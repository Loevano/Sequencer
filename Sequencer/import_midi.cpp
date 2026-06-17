// ========================= import_midi.cpp =========================
#include "import_midi.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string_view>

// ------------------------------------------------------------
// Menu CCs (your mapping)
// ------------------------------------------------------------
static constexpr int CC_BANK  = 53;
static constexpr int CC_MUTE  = 54;
static constexpr int CC_SOLO  = 55;
static constexpr int CC_CLEAR = 56;

static constexpr int CC_SEND_SELECT_1 = 49;
static constexpr int CC_SEND_SELECT_2 = 50;
static constexpr int CC_TRACK_SELECT_PREV = 51;
static constexpr int CC_TRACK_SELECT_NEXT = 52;
static constexpr UInt8 MIDI_CH = 0; // 0 = channel 1

static constexpr intptr_t SRC_LCXL  = 1;
static constexpr intptr_t SRC_CLOCK = 2;

// ------------------------------------------------------------
// Helpers (device discovery / debug)
// ------------------------------------------------------------
static bool endpointNameContains(MIDIEndpointRef ep, std::string_view needle) {
    CFStringRef cfName = nullptr;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &cfName) != noErr || !cfName) return false;

    char name[256];
    const bool ok = CFStringGetCString(cfName, name, sizeof(name), kCFStringEncodingUTF8);
    CFRelease(cfName);

    return ok && (std::string_view{name}.find(needle) != std::string_view::npos);
}

static void printMidiSourcesOnce() {
    const ItemCount nSrc = MIDIGetNumberOfSources();
    std::cout << "---- MIDI Sources (" << nSrc << ") ----\n";
    for (ItemCount i = 0; i < nSrc; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        CFStringRef cfName = nullptr;
        if (MIDIObjectGetStringProperty(src, kMIDIPropertyName, &cfName) == noErr && cfName) {
            char name[256];
            if (CFStringGetCString(cfName, name, sizeof(name), kCFStringEncodingUTF8))
                std::cout << "Source[" << i << "]: " << name << "\n";
            CFRelease(cfName);
        }
    }
}

static void printMidiDestinationsOnce() {
    const ItemCount nDest = MIDIGetNumberOfDestinations();
    std::cout << "---- MIDI Destinations (" << nDest << ") ----\n";
    for (ItemCount i = 0; i < nDest; ++i) {
        MIDIEndpointRef dst = MIDIGetDestination(i);
        CFStringRef cfName = nullptr;
        if (MIDIObjectGetStringProperty(dst, kMIDIPropertyName, &cfName) == noErr && cfName) {
            char name[256];
            if (CFStringGetCString(cfName, name, sizeof(name), kCFStringEncodingUTF8))
                std::cout << "Dest[" << i << "]: " << name << "\n";
            CFRelease(cfName);
        }
    }
}

static Action actionFromCc(int cc) {
    if (cc == CC_MUTE)  return MUTE;
    if (cc == CC_SOLO)  return SOLO;
    if (cc == CC_CLEAR) return CLEAR;
    return NONE;
}

// ------------------------------------------------------------
// Ctor / dtor / init
// ------------------------------------------------------------
MidiInterface::MidiInterface(std::vector<std::vector<Sequencer>>* allBanks,
                             bool* b,
                             int* globalStep,
                             bool debug)
: debugLogging(debug), banks(allBanks), bank(b), playStep(globalStep)
{
    for (int u = 0; u < kUserChannels; ++u)
        for (int t = 0; t < 16; ++t)
            trackVelScale[u][t] = 127;
}

MidiInterface::~MidiInterface() {
    if (inputPort)     MIDIPortDispose(inputPort);
    if (outputPort)    MIDIPortDispose(outputPort);
    if (virtualDest)   MIDIEndpointDispose(virtualDest);
    if (virtualSource) MIDIEndpointDispose(virtualSource);
    if (client)        MIDIClientDispose(client);
}

bool MidiInterface::initialize() {
    OSStatus r = noErr;

    if (debugLogging) std::cerr << "CoreMIDI: creating client\n";
    r = MIDIClientCreate(CFSTR("lcxl_client"), nullptr, nullptr, &client);
    if (r != noErr) { std::cerr << "MIDIClientCreate failed\n"; return false; }

    if (debugLogging) std::cerr << "CoreMIDI: creating input port\n";
    r = MIDIInputPortCreate(client, CFSTR("in"), midiCallback, this, &inputPort);
    if (r != noErr) { std::cerr << "MIDIInputPortCreate failed\n"; return false; }

    if (debugLogging) std::cerr << "CoreMIDI: creating output port\n";
    r = MIDIOutputPortCreate(client, CFSTR("out"), &outputPort);
    if (r != noErr) { std::cerr << "MIDIOutputPortCreate failed\n"; return false; }

    if (debugLogging) std::cerr << "CoreMIDI: creating virtual source\n";
    r = MIDISourceCreate(client, CFSTR("Sequencer Out"), &virtualSource);
    if (r != noErr) { std::cerr << "MIDISourceCreate failed\n"; return false; }

    // Virtual IN from Ableton (must have callback)
    if (debugLogging) std::cerr << "CoreMIDI: creating virtual destination\n";
    r = MIDIDestinationCreate(client, CFSTR("Sequencer In"), midiCallback, this, &virtualDest);
    if (r != noErr) { std::cerr << "MIDIDestinationCreate failed\n"; return false; }

    if (debugLogging) {
        printMidiSourcesOnce();
        printMidiDestinationsOnce();
    }

    bool connectedLcxl  = false;
    bool connectedClock = false;

    if (debugLogging) std::cerr << "CoreMIDI: scanning sources\n";
    const ItemCount nSrc = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < nSrc; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);

        if (endpointNameContains(src, "Launch Control XL")) {
            if (MIDIPortConnectSource(inputPort, src, (void*)SRC_LCXL) == noErr) {
                connectedLcxl = true;
                if (debugLogging)
                    std::cout << "Connected to Launch Control XL (Source[" << i << "])\n";
            }
        }

        // You used "MIDI Port" as your clock source label — keep it as-is.
        if (endpointNameContains(src, "MIDI Port")) {
            if (MIDIPortConnectSource(inputPort, src, (void*)SRC_CLOCK) == noErr) {
                connectedClock = true;
                if (debugLogging)
                    std::cout << "Connected to MIDI Port (Source[" << i << "])\n";
            }
        }
    }
    if (debugLogging) std::cerr << "CoreMIDI: source scan complete\n";

    if (!connectedLcxl)  std::cerr << "ERROR: Launch Control XL not connected\n";
    if (!connectedClock) {
        std::cerr << "WARNING: MIDI Port (clock) not connected; "
                     "clock can still arrive through Sequencer In\n";
    }

    // Fallback: connect everything if LCXL wasn't matched
    if (!connectedLcxl) {
        std::cerr << "Warning: no Launch Control source matched. Connecting ALL sources.\n";
        for (ItemCount i = 0; i < nSrc; ++i) {
            MIDIPortConnectSource(inputPort, MIDIGetSource(i), nullptr);
        }
    }

    if (debugLogging)
        std::cout << "Virtual ports created: Sequencer In (from Ableton), Sequencer Out (to Ableton)\n";

    return true;
}

// ------------------------------------------------------------
// MIDI sending
// ------------------------------------------------------------
void MidiInterface::sendMsg3(UInt8 status, UInt8 data1, UInt8 data2) {
    if (!virtualSource && !outputPort) return;

    UInt8 data[3] = { status, data1, data2 };

    MIDIPacketList packetList;
    MIDIPacket* packet = MIDIPacketListInit(&packetList);
    MIDIPacketListAdd(&packetList, sizeof(packetList), packet, 0, 3, data);

    // Virtual source -> Ableton / other apps
    if (virtualSource) MIDIReceived(virtualSource, &packetList);

    // Optional: mirror to hardware destinations (same behavior as your original)
    if (outputPort) {
        const ItemCount nDest = MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < nDest; ++i)
            MIDISend(outputPort, MIDIGetDestination(i), &packetList);
    }
}

void MidiInterface::sendSysEx(const std::vector<UInt8>& data) {
    if (!outputPort || data.empty()) return;

    MIDIPacketList packetList;
    MIDIPacket* packet = MIDIPacketListInit(&packetList);
    MIDIPacketListAdd(&packetList,
                      sizeof(packetList),
                      packet,
                      0,
                      (UInt16)data.size(),
                      data.data());

    const ItemCount nDest = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < nDest; ++i)
        MIDISend(outputPort, MIDIGetDestination(i), &packetList);
}

void MidiInterface::sendCC(int cc, int value) {
    value = std::clamp(value, 0, 127);
    sendMsg3((UInt8)(0xB0 | MIDI_CH), (UInt8)cc, (UInt8)value);
}

void MidiInterface::sendNoteOn(int note, int vel, int channel) {
    note = std::clamp(note, 0, 127);
    vel  = std::clamp(vel, 1, 127);
    channel = std::clamp(channel, 0, 15);
    sendMsg3((UInt8)(0x90 | channel), (UInt8)note, (UInt8)vel);
}

void MidiInterface::sendNoteOff(int note, int channel) {
    note = std::clamp(note, 0, 127);
    channel = std::clamp(channel, 0, 15);
    sendMsg3((UInt8)(0x80 | channel), (UInt8)note, 0);
}

void MidiInterface::allNotesOff() {
    if (!banks) return;
    for (int u = 0; u < (int)banks->size(); ++u) {
        const auto& tracks = (*banks)[u];
        for (int t = 0; t < (int)tracks.size(); ++t)
            sendNoteOff(trackToNote(t), u);
    }
    clearRotaryLeds();
    sendCC(123, 0); // CC123 All Notes Off (safety net)
}

// ------------------------------------------------------------
// Clock scheduling
// ------------------------------------------------------------
void MidiInterface::resetClockState() {
    midiClockPulses = 0;
    clockStep = 0;
    pendingSwingStep = -1;
    pendingSwingPulse = 0;
    earlySwingStep = -1;
    lastTickStep = -1;
    if (playStep) *playStep = 0;
}

int MidiInterface::currentStepCount() const {
    if (!banks || activeUser < 0 || activeUser >= (int)banks->size()) return 0;

    const auto& tracks = (*banks)[activeUser];
    const int selected = selectedByUser[activeUser];
    if (selected < 0 || selected >= (int)tracks.size()) return 0;

    return tracks[selected].getNumSteps();
}

void MidiInterface::playPendingSwingStepIfDue() {
    if (pendingSwingStep < 0 || midiClockPulses < pendingSwingPulse) return;

    if (playStep) *playStep = pendingSwingStep;
    tickStep(pendingSwingStep);

    pendingSwingStep = -1;
    pendingSwingPulse = 0;
}

void MidiInterface::playEarlySwingStepIfDue() {
    const int swing = swingPulses.load();
    if (swing >= 0 || earlySwingStep >= 0) return;

    const int steps = currentStepCount();
    if (steps <= 0) return;

    const int earlyPulses = -swing;
    if ((midiClockPulses + earlyPulses) % kPulsesPer16th != 0) return;

    const int upcoming = (clockStep + 1) % steps;
    if ((upcoming % 2) == 0) return;

    if (playStep) *playStep = upcoming;
    tickStep(upcoming);
    earlySwingStep = upcoming;
}

void MidiInterface::scheduleOrPlayStep(int step) {
    const int swing = swingPulses.load();
    const bool swingStep = swing > 0 && (step % 2) == 1;
    if (swingStep) {
        pendingSwingStep = step;
        pendingSwingPulse = midiClockPulses + swing;
        return;
    }

    if (playStep) *playStep = step;
    tickStep(step);
}

void MidiInterface::advanceMidiClockPulse() {
    if (!transportRunning || !playStep || !banks) return;

    midiClockPulses++;
    playPendingSwingStepIfDue();
    playEarlySwingStepIfDue();

    if (midiClockPulses % kPulsesPer16th != 0) return;

    const int steps = currentStepCount();
    if (steps <= 0) return;

    clockStep = (clockStep + 1) % steps;
    if (earlySwingStep == clockStep) {
        earlySwingStep = -1;
        return;
    }

    scheduleOrPlayStep(clockStep);
}

// ------------------------------------------------------------
// Audio/mixer logic
// ------------------------------------------------------------
bool MidiInterface::anyTrackSoloed(const std::vector<Sequencer>& tracks) const {
    for (const auto& tr : tracks)
        if (tr.isSoloed()) return true;
    return false;
}

bool MidiInterface::trackAudible(const Sequencer& tr, const std::vector<Sequencer>& tracks) const {
    const bool anySolo = anyTrackSoloed(tracks);
    if (tr.isMuted()) return false;
    if (anySolo && !tr.isSoloed()) return false;
    return true;
}

bool MidiInterface::anyChannelSoloed() const {
    for (bool soloed : channelSoloed)
        if (soloed) return true;
    return false;
}

bool MidiInterface::channelAudible(int user) const {
    if (user < 0 || user >= kUserChannels) return false;
    if (channelMuted[user]) return false;
    if (anyChannelSoloed() && !channelSoloed[user]) return false;
    return true;
}

// ------------------------------------------------------------
// Tick (called from MIDI clock or internal clock)
// ------------------------------------------------------------
void MidiInterface::tickStep(int step) {
    if (!banks || banks->empty()) return;

    // Note OFF previous step (1-step gate)
    if (lastTickStep >= 0) {
        for (int u = 0; u < (int)banks->size(); ++u) {
            const auto& tracks = (*banks)[u];
            for (int t = 0; t < (int)tracks.size(); ++t) {
                const Sequencer& tr = tracks[t];
                const bool wasOn = (u < kUserChannels && t < 16 && trackNoteOn[u][t]);
                if (tr.getStepOn(lastTickStep) || wasOn) {
                    sendNoteOff(trackToNote(t), u);
                    if (u < kUserChannels && t < 16) trackNoteOn[u][t] = false;
                }
            }
        }
    }

    // Note ON current step
    for (int u = 0; u < (int)banks->size(); ++u) {
        if (!channelAudible(u)) continue;

        const auto& tracks = (*banks)[u];
        for (int t = 0; t < (int)tracks.size(); ++t) {
            const Sequencer& tr = tracks[t];
            if (!trackAudible(tr, tracks)) continue;

            if (tr.getStepOn(step)) {
                int vel = tr.getVelocity(step);

                // apply per-track pot scale (CC1..16)
                if (t < 16) vel = (vel * trackVelScale[u][t]) / 127;

                if (vel > 0) {
                    sendNoteOn(trackToNote(t), vel, u);
                    if (u < kUserChannels && t < 16) {
                        trackNoteOn[u][t] = true;
                        rotaryLedUntil[u][t] = std::chrono::steady_clock::now() +
                                               std::chrono::milliseconds(kRotaryPulseMs);
                    }
                }
            }
        }
    }

    lastTickStep = step;
}

// ------------------------------------------------------------
// Launch Control XL LED encoding
// ------------------------------------------------------------
int MidiInterface::lcxlLedValue(std::string_view spec) const {
    auto eq = [](std::string_view a, std::string_view b){ return a == b; };

    std::string_view color = "off";
    std::string_view level = "full";
    bool flash = false;

    size_t start = 0;
    while (start < spec.size()) {
        size_t end = spec.find(':', start);
        if (end == std::string_view::npos) end = spec.size();
        std::string_view tok = spec.substr(start, end - start);

        if (eq(tok, "off") || eq(tok, "red") || eq(tok, "green") || eq(tok, "amber") || eq(tok, "yellow"))
            color = tok;
        else if (eq(tok, "low") || eq(tok, "mid") || eq(tok, "full"))
            level = tok;
        else if (eq(tok, "flash"))
            flash = true;

        start = end + 1;
    }

    if (eq(color, "off")) return 0;

    int lv = 3;
    if (eq(level, "low")) lv = 1;
    else if (eq(level, "mid")) lv = 2;

    int red = 0, green = 0;
    if (eq(color, "red"))        { red = lv; green = 0; }
    else if (eq(color, "green")) { red = 0;  green = lv; }
    else if (eq(color, "amber")) { red = lv; green = lv; }
    else if (eq(color, "yellow")) {
        red   = (lv >= 2 ? 2 : 1);
        green = lv;
    }

    const int flags = flash ? 8 : 12;
    return std::clamp(flags + (16 * green) + red, 0, 127);
}

void MidiInterface::setLed(int cc, std::string_view spec) {
    sendCC(cc, lcxlLedValue(spec));
}

void MidiInterface::setRotaryLed(int index, std::string_view spec) {
    if (index < 0 || index >= 24) return;

    const UInt8 value = (UInt8)lcxlLedValue(spec);
    for (int tmpl = 0; tmpl < 16; ++tmpl) {
        std::vector<UInt8> data = {
            0xF0, 0x00, 0x20, 0x29, 0x02, 0x11, 0x78,
            (UInt8)tmpl,
            (UInt8)index,
            value,
            0xF7
        };
        sendSysEx(data);
    }
}

void MidiInterface::clearRotaryLeds() {
    for (int u = 0; u < kUserChannels; ++u) {
        for (int t = 0; t < 16; ++t) {
            trackNoteOn[u][t] = false;
            rotaryLedUntil[u][t] = {};
        }
    }

    for (int t = 0; t < 24; ++t) {
        if (t < 16) rotaryLedShown[t] = false;
        setRotaryLed(t, "off");
    }
}

// ------------------------------------------------------------
// LED Updates
// ------------------------------------------------------------
void MidiInterface::updateMenuLeds() {
    if (!bank) return;
    setLed(CC_BANK, *bank ? "green:full" : "off");

    const Action shownAction = channelSelectHeld ? channelAction : action;
    setLed(CC_MUTE, shownAction == MUTE ? "amber:full" : "off");
    setLed(CC_SOLO, shownAction == SOLO ? "amber:full" : "off");

    if (channelSelectHeld) setLed(CC_CLEAR, "green:full");
    else                   setLed(CC_CLEAR, action == CLEAR ? "amber:full" : "off");

    if (*bank) {
        std::string_view leftSwingLed = "off";
        std::string_view rightSwingLed = "off";

        const int swing = swingPulses.load();
        const int swingMagnitude = swing < 0 ? -swing : swing;
        if (swingMagnitude > 0) {
            std::string_view swingLed = "amber:full";
            if (swingMagnitude >= 2) {
                const auto now = std::chrono::steady_clock::now();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()).count();
                const int blinkMs = (swingMagnitude == 2) ? 450 : 150;
                swingLed = ((ms / blinkMs) % 2 == 0) ? "amber:full" : "off";
            }

            if (swing < 0) leftSwingLed = swingLed;
            else           rightSwingLed = swingLed;
        }

        setLed(CC_TRACK_SELECT_PREV, leftSwingLed);
        setLed(CC_TRACK_SELECT_NEXT, rightSwingLed);
    } else {
        setLed(CC_TRACK_SELECT_PREV, "off");
        setLed(CC_TRACK_SELECT_NEXT, "off");
    }
}

void MidiInterface::updateRotaryLeds() {
    if (activeUser < 0 || activeUser >= kUserChannels) return;

    const auto now = std::chrono::steady_clock::now();
    for (int t = 0; t < 16; ++t) {
        const bool lit = now < rotaryLedUntil[activeUser][t];
        if (lit == rotaryLedShown[t]) continue;

        rotaryLedShown[t] = lit;
        setRotaryLed(t, lit ? "amber:full" : "off");
    }
}

void MidiInterface::updateStepLeds(int baseCC) {
    if (!banks || !playStep) return;
    if (activeUser < 0 || activeUser >= (int)banks->size()) return;
    (void)baseCC;

    const auto& tracks = (*banks)[activeUser];
    const int selected = selectedByUser[activeUser];
    if (selected < 0 || selected >= (int)tracks.size()) return;

    if (channelSelectHeld) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() >= blinkIntervalMs) {
            blinkOn = !blinkOn;
            lastBlink = now;
        }

        const int count = std::min(kUserChannels, (int)stepCCs.size());
        for (int i = 0; i < count; ++i) {
            switch (channelAction) {
                case MUTE:
                    if (!channelMuted[i]) setLed(stepCCs[i], "green:full");
                    else                  setLed(stepCCs[i], blinkOn ? "red:full" : "off");
                    break;
                case SOLO:
                    setLed(stepCCs[i], channelSoloed[i] ? (blinkOn ? "amber:full" : "off") : "off");
                    break;
                case NONE:
                default:
                    setLed(stepCCs[i], (i == activeUser) ? "green:full" : "off");
                    break;
            }
        }
        return;
    }

    const Sequencer& seq = tracks[selected];
    const int steps = seq.getNumSteps();
    if (steps <= 0) return;

    const int cur = (*playStep) % steps;

    const int count = std::min(steps, (int)stepCCs.size());
    for (int i = 0; i < count; ++i) {
        if (i == cur) { setLed(stepCCs[i], "red:full"); continue; }

        const int v = seq.getVelocity(i);
        if (v == 0) setLed(stepCCs[i], "off");
        else if (v <= kVelLevels[0]) setLed(stepCCs[i], "amber:low");
        else if (v <= kVelLevels[1]) setLed(stepCCs[i], "amber:mid");
        else setLed(stepCCs[i], "amber:full");
    }
}

void MidiInterface::updateBankLeds() {
    if (!banks) return;
    if (activeUser < 0 || activeUser >= (int)banks->size()) return;

    const auto& tracks = (*banks)[activeUser];
    const int selected = selectedByUser[activeUser];

    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() >= blinkIntervalMs) {
        blinkOn = !blinkOn;
        lastBlink = now;
    }

    for (int i = 0; i < (int)tracks.size(); ++i) {
        const Sequencer& t = tracks[i];

        if (action == NONE) {
            if (i == selected) {
                if (t.hasSteps()) setLed(stepCCs[i], blinkOn ? "green:full" : "amber:mid");
                else              setLed(stepCCs[i], blinkOn ? "green:full" : "off");
            } else if (t.hasSteps()) setLed(stepCCs[i], "red:mid");
            else                     setLed(stepCCs[i], "off");
            continue;
        }

        switch (action) {
            case MUTE:
                if (!t.isMuted()) setLed(stepCCs[i], "green:full");
                else              setLed(stepCCs[i], blinkOn ? "red:full" : "off");
                break;
            case SOLO:
                setLed(stepCCs[i], t.isSoloed() ? (blinkOn ? "amber:full" : "off") : "off");
                break;
            case CLEAR:
                setLed(stepCCs[i], t.hasSteps() ? (blinkOn ? "amber:full" : "off") : "off");
                break;
            default:
                setLed(stepCCs[i], "off");
                break;
        }
    }
}

// ------------------------------------------------------------
// MIDI callback (buttons + MIDI clock sync) [SOURCE-TAGGED]
// ------------------------------------------------------------
void MidiInterface::midiCallback(const MIDIPacketList* list,
                                 void* refCon,
                                 void* srcConnRefCon)
{
    auto* self = static_cast<MidiInterface*>(refCon);
    if (!self) return;

    const intptr_t srcTag = (intptr_t)srcConnRefCon;
    const bool fromLcxl  = (srcTag == SRC_LCXL);
    const bool fromClock = (srcTag == SRC_CLOCK);
    const bool allowRealtimeClock = fromClock || !fromLcxl;

    const MIDIPacket* pkt = &list->packet[0];

    auto isPress   = [](int v){ return v == 127; };
    auto isRelease = [](int v){ return v == 0;   };

    for (UInt32 p = 0; p < list->numPackets; ++p) {
        const UInt8* data = pkt->data;
        const UInt16 len  = pkt->length;

        for (UInt16 idx = 0; idx < len; ) {
            const UInt8 status = data[idx];

            // -------- System Real-Time (1 byte) --------
            if (status >= 0xF8) {
                if (!allowRealtimeClock) { idx += 1; continue; }

                if (self->useMidiClock) {
                    switch (status) {
                        case 0xFA: // Start
                            self->transportRunning = true;
                            self->resetClockState();
                            self->allNotesOff();
                            break;

                        case 0xFB: // Continue
                            self->transportRunning = true;
                            self->clockStep = self->playStep ? *self->playStep : 0;
                            self->pendingSwingStep = -1;
                            self->pendingSwingPulse = 0;
                            self->earlySwingStep = -1;
                            break;

                        case 0xFC: // Stop
                            self->transportRunning = false;
                            self->resetClockState();
                            self->allNotesOff();
                            break;

                        case 0xF8: // Clock pulse
                            self->advanceMidiClockPulse();
                            break;

                        default: break;
                    }
                }

                idx += 1;
                continue;
            }

            // -------- Channel Voice 3-byte --------
            const UInt8 type = status & 0xF0;
            if (type == 0xB0 || type == 0x90 || type == 0x80) {
                if (idx + 2 >= len) break;

                const int d1 = data[idx + 1];
                const int d2 = data[idx + 2];

                // ---- Only handle CCs from LCXL ----
                if (type == 0xB0) {
                    if (!fromLcxl) { idx += 3; continue; }

                    const int cc    = d1;
                    const int value = d2;

                    // Pots CC1..16: per-track velocity scale
                    if (cc >= 1 && cc <= 16) {
                        const int t = cc - 1;
                        if (self->banks && self->activeUser < (int)self->banks->size()) {
                            self->trackVelScale[self->activeUser][t] = std::clamp(value, 0, 127);
                        }
                        idx += 3;
                        continue;
                    }

                    // BANK (CC53) momentary
                    if (cc == CC_BANK) {
                        if (self->bank) *self->bank = isPress(value);

                        if (isRelease(value)) {
                            self->action = NONE;
                            self->exitStateOnRelease = false;
                            self->heldStateCc = -1;
                            self->heldStateAction = NONE;
                            self->soloClearedDuringHold = false;
                            self->soloButtonHeld = false;
                            self->channelSelectHeld = false;
                            self->channelAction = NONE;
                            self->exitChannelStateOnRelease = false;
                            self->heldChannelStateCc = -1;
                            self->heldChannelStateAction = NONE;
                        }

                        idx += 3;
                        continue;
                    }

                    const bool inBank = (self->bank && *self->bank);

                    // Channel select modifier (CC56) in step mode only
                    if (!inBank && cc == CC_CLEAR) {
                        self->channelSelectHeld = isPress(value);
                        if (isRelease(value)) {
                            self->channelAction = NONE;
                            self->exitChannelStateOnRelease = false;
                            self->heldChannelStateCc = -1;
                            self->heldChannelStateAction = NONE;
                        }
                        idx += 3;
                        continue;
                    }

                    // Channel mute/solo actions while the channel-select modifier is held
                    if (!inBank && self->channelSelectHeld && (cc == CC_MUTE || cc == CC_SOLO)) {
                        const Action pressedAction = actionFromCc(cc);

                        if (isPress(value)) {
                            if (self->channelAction != pressedAction) {
                                self->channelAction = pressedAction;
                                self->exitChannelStateOnRelease = false;
                                self->heldChannelStateCc = cc;
                                self->heldChannelStateAction = pressedAction;
                                idx += 3;
                                continue;
                            }

                            self->exitChannelStateOnRelease = true;
                            self->heldChannelStateCc = cc;
                            self->heldChannelStateAction = pressedAction;
                            idx += 3;
                            continue;
                        }

                        if (isRelease(value)) {
                            if (self->heldChannelStateCc == cc) {
                                if (self->heldChannelStateAction == self->channelAction &&
                                    self->exitChannelStateOnRelease) {
                                    self->channelAction = NONE;
                                }
                                self->exitChannelStateOnRelease = false;
                                self->heldChannelStateCc = -1;
                                self->heldChannelStateAction = NONE;
                            }
                            idx += 3;
                            continue;
                        }
                    }

                    // Swing amount (CC51/52) - bank held, press only
                    if (inBank && isPress(value)) {
                        if (cc == CC_TRACK_SELECT_PREV) {
                            self->adjustSwingPulses(-1);
                            idx += 3;
                            continue;
                        }
                        if (cc == CC_TRACK_SELECT_NEXT) {
                            self->adjustSwingPulses(+1);
                            idx += 3;
                            continue;
                        }
                    }

                    // Default velocity levels (CC49/50) - step mode only, press only
                    if (!inBank && isPress(value)) {
                        if (cc == CC_SEND_SELECT_1) {
                            if (!self->adjustHeldStepVelocities(+1)) self->adjustDefaultVelLevel(+1);
                            idx += 3;
                            continue;
                        }
                        if (cc == CC_SEND_SELECT_2) {
                            if (!self->adjustHeldStepVelocities(-1)) self->adjustDefaultVelLevel(-1);
                            idx += 3;
                            continue;
                        }
                    }

                    // Track Select buttons: alternate selected device/track navigation.
                    if (!inBank && isPress(value) && (cc == CC_TRACK_SELECT_PREV || cc == CC_TRACK_SELECT_NEXT)) {
                        if (self->banks && self->activeUser >= 0 && self->activeUser < (int)self->banks->size()) {
                            const int count = std::min((int)(*self->banks)[self->activeUser].size(), 16);
                            if (count > 0) {
                                const int cur = std::clamp(self->selectedByUser[self->activeUser], 0, count - 1);
                                const int dir = (cc == CC_TRACK_SELECT_NEXT) ? 1 : -1;
                                self->selectedByUser[self->activeUser] = std::clamp(cur + dir, 0, count - 1);
                            }
                        }
                        idx += 3;
                        continue;
                    }

                    // SOLO physical hold bookkeeping (menu only)
                    if (inBank && cc == CC_SOLO) {
                        if (isPress(value))   self->soloButtonHeld = true;
                        if (isRelease(value)) self->soloButtonHeld = false;
                    }

                    // While BANK held AND SOLO held, press CLEAR clears solos
                    if (inBank && self->soloButtonHeld && cc == CC_CLEAR && isPress(value)) {
                        if (self->banks && self->activeUser < (int)self->banks->size()) {
                            auto& tracks = (*self->banks)[self->activeUser];
                            for (auto& tr : tracks)
                                if (tr.isSoloed()) tr.toggleSolo();
                        }
                        self->soloClearedDuringHold = true;
                        self->exitStateOnRelease = false;
                        idx += 3;
                        continue;
                    }

                    // State buttons (CC54/55/56) while BANK held
                    if (inBank && (cc == CC_MUTE || cc == CC_SOLO || cc == CC_CLEAR)) {
                        const Action pressedAction = actionFromCc(cc);

                        if (isPress(value)) {
                            if (self->action != pressedAction) {
                                self->action = pressedAction;
                                self->exitStateOnRelease = false;
                                self->heldStateCc = cc;
                                self->heldStateAction = pressedAction;
                                if (pressedAction == SOLO) self->soloClearedDuringHold = false;
                                idx += 3;
                                continue;
                            }

                            // press again in same state -> arm exit (unless SOLO protected)
                            self->exitStateOnRelease = !(pressedAction == SOLO && self->soloClearedDuringHold);
                            self->heldStateCc = cc;
                            self->heldStateAction = pressedAction;

                            idx += 3;
                            continue;
                        }

                        if (isRelease(value)) {
                            if (self->heldStateCc == cc) {
                                if (self->heldStateAction == self->action && self->exitStateOnRelease) {
                                    if (!(self->action == SOLO && self->soloClearedDuringHold))
                                        self->action = NONE;
                                }
                                self->exitStateOnRelease = false;
                                self->heldStateCc = -1;
                                self->heldStateAction = NONE;
                            }
                            idx += 3;
                            continue;
                        }
                    }

                    // Pads CC33–48
                    auto it = std::find(self->stepCCs.begin(), self->stepCCs.end(), cc);
                    if (it != self->stepCCs.end()) {
                        const int index = (int)std::distance(self->stepCCs.begin(), it);

                        if (inBank) {
                            if (isPress(value) && self->banks && self->activeUser < (int)self->banks->size()) {
                                auto& tracks = (*self->banks)[self->activeUser];
                                if (index >= (int)tracks.size()) { idx += 3; continue; }
                                Sequencer& tr = tracks[index];
                                switch (self->action) {
                                    case CLEAR: tr.clearSteps(); break;
                                    case MUTE:  tr.toggleMute(); break;
                                    case SOLO:  tr.toggleSolo(); break;
                                    case NONE:
                                    default:    self->selectedByUser[self->activeUser] = index; break;
                                }
                            }
                        } else if (self->channelSelectHeld) {
                            if (isPress(value) && index < kUserChannels) {
                                switch (self->channelAction) {
                                    case MUTE:
                                        self->channelMuted[index] = !self->channelMuted[index];
                                        break;
                                    case SOLO:
                                        self->channelSoloed[index] = !self->channelSoloed[index];
                                        break;
                                    case NONE:
                                    default:
                                        if (self->banks && index < (int)self->banks->size())
                                            self->activeUser = index;
                                        break;
                                }
                            }
                        } else {
                            if (!self->banks || self->activeUser >= (int)self->banks->size()) { idx += 3; continue; }
                            auto& tracks = (*self->banks)[self->activeUser];
                            const int sel = self->selectedByUser[self->activeUser];
                            if (sel < 0 || sel >= (int)tracks.size()) { idx += 3; continue; }
                            Sequencer& seq = tracks[sel];

                            if (isPress(value)) {
                                self->stepHeld[index] = true;
                                self->stepEditedWhileHeld[index] = false;
                                if (!seq.getStepOn(index)) {
                                    seq.setStepOn(index, true);
                                    seq.setVelocity(index, self->defaultVelocity());
                                    self->pendingOff[index] = false;
                                } else {
                                    self->pendingOff[index] = true;
                                }
                            } else if (isRelease(value)) {
                                if (self->pendingOff[index] && !self->stepEditedWhileHeld[index]) {
                                    seq.setStepOn(index, false);
                                }
                                self->pendingOff[index] = false;
                                self->stepHeld[index] = false;
                                self->stepEditedWhileHeld[index] = false;
                            }
                        }

                        idx += 3;
                        continue;
                    }

                    // unhandled CC
                    idx += 3;
                    continue;
                }

                // skip unhandled note/voice message
                idx += 3;
                continue;
            }

            // Unknown: skip 1 byte
            idx += 1;
        }

        pkt = MIDIPacketNext(pkt);
    }
}


// ------------------------------------------------------------
// Velocity helpers
// ------------------------------------------------------------
void MidiInterface::adjustDefaultVelLevel(int dir) {
    defaultVelLevel = std::clamp(defaultVelLevel + dir, 0, 2);
}

void MidiInterface::adjustSwingPulses(int dir) {
    const int swing = std::clamp(swingPulses.load() + dir, -kMaxSwingPulses, kMaxSwingPulses);
    swingPulses.store(swing);
    pendingSwingStep = -1;
    pendingSwingPulse = 0;
    earlySwingStep = -1;

    std::cout << "Swing: " << (6 + swing) << "/" << (6 - swing)
              << " MIDI clock pulses\n";
}

bool MidiInterface::adjustHeldStepVelocities(int dir) {
    if (!banks || activeUser < 0 || activeUser >= (int)banks->size()) return false;

    auto& tracks = (*banks)[activeUser];
    const int sel = selectedByUser[activeUser];
    if (sel < 0 || sel >= (int)tracks.size()) return false;

    Sequencer& seq = tracks[sel];
    bool changed = false;

    const int count = std::min(seq.getNumSteps(), (int)stepCCs.size());
    for (int step = 0; step < count; ++step) {
        if (!stepHeld[step] || !seq.getStepOn(step)) continue;

        const int cur = seq.getVelocity(step);
        int level = 0;
        if (cur > kVelLevels[0]) level = 1;
        if (cur > kVelLevels[1]) level = 2;

        level = std::clamp(level + dir, 0, 2);
        seq.setVelocity(step, kVelLevels[level]);
        pendingOff[step] = false;
        stepEditedWhileHeld[step] = true;
        changed = true;
    }

    return changed;
}
