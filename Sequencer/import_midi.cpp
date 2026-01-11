#include "import_midi.hpp"
#include <algorithm>
#include <iostream>

// Menu CCs (your mapping)
static constexpr int CC_BANK  = 53;
static constexpr int CC_MUTE  = 54;
static constexpr int CC_SOLO  = 55;
static constexpr int CC_CLEAR = 56;

static constexpr int CC_SEND_SELECT_1  = 49;
static constexpr int CC_SEND_SELECT_2  = 50;
static constexpr int CC_USER_BTN_1     = 51; // labeled "Track Select" on hardware
static constexpr int CC_USER_BTN_2     = 52; // labeled "Track Select" on hardware

static constexpr UInt8 MIDI_CH = 0; // 0 = channel 1, 1 = channel 2, ... 15 = channel 16

MidiInterface::MidiInterface(std::vector<Sequencer>* t,
                             int* s,
                             bool* b,
                             int* globalStep)
: tracks(t), selected(s), bank(b), playStep(globalStep)
{}

MidiInterface::~MidiInterface() {
    if (inputPort)  MIDIPortDispose(inputPort);
    if (outputPort) MIDIPortDispose(outputPort);
    if (client)     MIDIClientDispose(client);
}

bool MidiInterface::initialize() {
    OSStatus r = noErr;

    r = MIDIClientCreate(CFSTR("lcxl_client"), nullptr, nullptr, &client);
    if (r != noErr) { std::cerr << "MIDIClientCreate failed\n"; return false; }

    r = MIDIInputPortCreate(client, CFSTR("in"), midiCallback, this, &inputPort);
    if (r != noErr) { std::cerr << "MIDIInputPortCreate failed\n"; return false; }

    r = MIDIOutputPortCreate(client, CFSTR("out"), &outputPort);
    if (r != noErr) { std::cerr << "MIDIOutputPortCreate failed\n"; return false; }

    const ItemCount nSrc = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < nSrc; ++i) {
        MIDIPortConnectSource(inputPort, MIDIGetSource(i), nullptr);
    }

    return true;
}

void MidiInterface::sendCC(int cc, int value) {
    if (!outputPort) return;


    UInt8 data[3] = { static_cast<UInt8>(0xB0 | MIDI_CH),
                      static_cast<UInt8>(cc),
                      static_cast<UInt8>(value) };

    MIDIPacketList packetList;
    MIDIPacket* packet = MIDIPacketListInit(&packetList);
    MIDIPacketListAdd(&packetList, sizeof(packetList), packet, 0, 3, data);

    const ItemCount nDest = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < nDest; ++i) {
        MIDISend(outputPort, MIDIGetDestination(i), &packetList);
    }
}

// ------------------------------------------------------------
// Launch Control XL LED encoding (Launchpad LED protocol)
//
// value = flags + (16 * greenLevel) + redLevel
// greenLevel/redLevel are 0..3. Flags commonly 12 for normal use,
// or 8 to use the flashing table (if flashing is configured).
// :contentReference[oaicite:2]{index=2}
// ------------------------------------------------------------
int MidiInterface::lcxlLedValue(std::string_view spec) const {
    // spec examples:
    // "off"
    // "green:full"
    // "red:low"
    // "amber:mid"
    // "yellow:full"
    // "amber:full:flash"

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

    if (eq(color, "off")) return 0; // easiest/cleanest off

    int lv = 3;
    if (eq(level, "low")) lv = 1;
    else if (eq(level, "mid")) lv = 2;
    else lv = 3;

    int red = 0, green = 0;

    if (eq(color, "red")) {
        red = lv; green = 0;
    } else if (eq(color, "green")) {
        red = 0; green = lv;
    } else if (eq(color, "amber")) {
        red = lv; green = lv;
    } else if (eq(color, "yellow")) {
        // "yellow" as stronger green with some red; tweak if you prefer:
        red = (lv >= 2 ? 2 : 1);
        green = lv;
    }

    const int flags = flash ? 8 : 12;
    int value = flags + (16 * green) + red;

    // clamp to MIDI 0..127
    if (value < 0) value = 0;
    if (value > 127) value = 127;
    return value;
}

void MidiInterface::setLed(int cc, std::string_view spec) {
    sendCC(cc, lcxlLedValue(spec));
}

// ------------------------------------------------------------
// LED Updates
// ------------------------------------------------------------
void MidiInterface::updateMenuLeds() {
    // BANK LED: on while held
    setLed(CC_BANK, *bank ? "green:full" : "off");

    // Action LEDs: on when selected (while bank held)
    setLed(CC_MUTE,  (action == MUTE)  ? "amber:full" : "off");
    setLed(CC_SOLO,  (action == SOLO)  ? "amber:full" : "off");
    setLed(CC_CLEAR, (action == CLEAR) ? "amber:full" : "off");
}

void MidiInterface::updateStepLeds(int baseCC) {
    const Sequencer& seq = (*tracks)[*selected];
    const int steps = seq.getNumSteps();
    const int cur = (*playStep) % steps;

    for (int i = 0; i < steps; ++i) {
        // Playhead overrides everything
        if (i == cur) {
            setLed(baseCC + i, "red:full");
            continue;
        }

        const int v = seq.getVelocity(i);

        if (v == 0) setLed(baseCC + i, "off");
        else if (v <= kVelLevels[0]) setLed(baseCC + i, "amber:low");
        else if (v <= kVelLevels[1]) setLed(baseCC + i, "amber:mid");
        else setLed(baseCC + i, "amber:full");

    }
}


void MidiInterface::updateBankLeds() {
    // blink timing
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastBlink).count() >= blinkIntervalMs) {
        blinkOn = !blinkOn;
        lastBlink = now;
    }

    for (int i = 0; i < (int)tracks->size(); ++i) {
        const Sequencer& t = (*tracks)[i];

        // Default in bank mode: track selection
        if (action == NONE) {
            if (i == *selected) {
                if (t.hasSteps()) {
                    // Selected track WITH notes:
                    // blink GREEN <-> AMBER
                    setLed(stepCCs[i], blinkOn ? "green:full" : "amber:mid");
                } else {
                    // Selected track WITHOUT notes:
                    // blink GREEN <-> OFF
                    setLed(stepCCs[i], blinkOn ? "green:full" : "off");
                }
            } else if (t.hasSteps()) {
                // Non-selected track with notes
                setLed(stepCCs[i], "amber:mid");
            } else {
                // Empty non-selected track
                setLed(stepCCs[i], "off");
            }
            continue;
        }
        
        // Requested per-state behaviour:
        switch (action) {
            case MUTE:
                // mute OFF -> LED ON
                // mute ON  -> LED BLINKING
                if (!t.isMuted()) setLed(stepCCs[i], "green:full");
                else setLed(stepCCs[i], blinkOn ? "red:full" : "off");
                break;

            case SOLO:
                // solo ON  -> LED BLINKING
                // solo OFF -> LED OFF
                setLed(stepCCs[i], t.isSoloed() ? (blinkOn ? "amber:full" : "off") : "off");
                break;

            case CLEAR:
                // has notes -> LED BLINKING
                // no notes  -> LED OFF
                setLed(stepCCs[i], t.hasSteps() ? (blinkOn ? "amber:full" : "off") : "off");
                break;

            default:
                setLed(stepCCs[i], "off");
                break;
        }
    }
}

void MidiInterface::midiCallback(const MIDIPacketList* list,
                                 void* ref,
                                 void*)
{
    auto* self = static_cast<MidiInterface*>(ref);
    const MIDIPacket* pkt = &list->packet[0];

    auto actionFromCc = [](int cc) -> Action {
        if (cc == CC_MUTE)  return MUTE;
        if (cc == CC_SOLO)  return SOLO;
        if (cc == CC_CLEAR) return CLEAR;
        return NONE;
    };

    for (UInt32 i = 0; i < list->numPackets; ++i) {
        const UInt8* d = pkt->data;

        if ((d[0] & 0xF0) == 0xB0) { // CC
            const int cc    = d[1];
            const int value = d[2];  // 127 press, 0 release (LCXL template)

            // ------------------------------------------------------------
            // BANK (CC53) momentary
            // ------------------------------------------------------------
            if (cc == CC_BANK) {
                *self->bank = (value == 127);

                if (value == 0) {
                    // leaving menu: return to note edit mode
                    self->action = NONE;

                    // reset state-exit bookkeeping
                    self->exitStateOnRelease = false;
                    self->heldStateCc = -1;
                    self->heldStateAction = NONE;
                    self->soloClearedDuringHold = false;

                    // reset SOLO physical hold
                    self->soloButtonHeld = false;
                }

                pkt = MIDIPacketNext(pkt);
                continue;
            }

            // ------------------------------------------------------------
            // Velocity up/down (CC49/50) - step mode only, press only
            // ------------------------------------------------------------
            if (value == 127 && !(*self->bank)) {
                if (cc == CC_SEND_SELECT_1) { // 49 higher
                    self->applyVelLevelToHeld(+1);
                    pkt = MIDIPacketNext(pkt);
                    continue;
                }
                if (cc == CC_SEND_SELECT_2) { // 50 lower
                    self->applyVelLevelToHeld(-1);
                    pkt = MIDIPacketNext(pkt);
                    continue;
                }
            }

            // ------------------------------------------------------------
            // Free buttons (CC51/52) press only
            // ------------------------------------------------------------
            if (value == 127 && (cc == CC_USER_BTN_1 || cc == CC_USER_BTN_2)) {
                self->handleUserButton(cc);
                pkt = MIDIPacketNext(pkt);
                continue;
            }

            // ------------------------------------------------------------
            // Track "SOLO button physically held" bookkeeping (menu only)
            // (This is what enables "hold SOLO + press CLEAR to clear solos")
            // ------------------------------------------------------------
            if (*self->bank && cc == CC_SOLO) {
                if (value == 127) self->soloButtonHeld = true;
                if (value == 0)   self->soloButtonHeld = false;
                // don't continue; we still want the state-button logic below
            }

            // ------------------------------------------------------------
            // SOLO-hold sub-action:
            // While BANK held AND SOLO is physically held, pressing CLEAR clears solos
            // WITHOUT switching to CLEAR state, and prevents exiting SOLO on CC55 release.
            // ------------------------------------------------------------
            if (*self->bank && self->soloButtonHeld && cc == CC_CLEAR && value == 127) {
                for (auto& tr : *self->tracks) {
                    if (tr.isSoloed()) tr.toggleSolo();
                }
                self->soloClearedDuringHold = true;
                self->exitStateOnRelease = false; // disarm any pending exit

                pkt = MIDIPacketNext(pkt);
                continue;
            }

            // ------------------------------------------------------------
            // State buttons (CC54/55/56) while BANK held:
            // - Press enters that state.
            // - If already in that state: pressing again ARMS exit-on-release.
            // - Release exits ONLY if exit was armed.
            // - Entering another state cancels pending exit.
            // Special: if solos were cleared during SOLO hold, do NOT exit SOLO on release.
            // ------------------------------------------------------------
            if (*self->bank && (cc == CC_MUTE || cc == CC_SOLO || cc == CC_CLEAR)) {
                const Action pressedAction = actionFromCc(cc);

                if (value == 127) { // press down
                    if (self->action != pressedAction) {
                        // switching to another state
                        self->action = pressedAction;

                        // cancel any pending exit
                        self->exitStateOnRelease = false;

                        // remember which state button is currently held
                        self->heldStateCc = cc;
                        self->heldStateAction = pressedAction;

                        // entering SOLO resets the gate for this hold
                        if (pressedAction == SOLO) {
                            self->soloClearedDuringHold = false;
                        }

                        pkt = MIDIPacketNext(pkt);
                        continue;
                    }

                    // pressing the SAME state button while already in that state -> arm exit on release
                    if (!(pressedAction == SOLO && self->soloClearedDuringHold)) {
                        self->exitStateOnRelease = true;
                    } else {
                        self->exitStateOnRelease = false;
                    }

                    self->heldStateCc = cc;
                    self->heldStateAction = pressedAction;

                    pkt = MIDIPacketNext(pkt);
                    continue;
                }

                if (value == 0) { // release
                    // If this is the button we were tracking, decide whether to exit
                    if (self->heldStateCc == cc) {
                        if (self->heldStateAction == self->action && self->exitStateOnRelease) {
                            // exit unless SOLO was "protected" by clearing during hold
                            if (!(self->action == SOLO && self->soloClearedDuringHold)) {
                                self->action = NONE;
                            }
                        }

                        // clear hold bookkeeping
                        self->exitStateOnRelease = false;
                        self->heldStateCc = -1;
                        self->heldStateAction = NONE;
                    }

                    pkt = MIDIPacketNext(pkt);
                    continue;
                }
            }

            // ------------------------------------------------------------
            // Pads CC33–48 (stepCCs)
            // ------------------------------------------------------------
            auto it = std::find(self->stepCCs.begin(), self->stepCCs.end(), cc);
            if (it != self->stepCCs.end()) {
                const int index = (int)std::distance(self->stepCCs.begin(), it); // 0..15

                if (*self->bank) {
                    // bank mode: act on press only
                    if (value == 127) {
                        Sequencer& tr = (*self->tracks)[index];
                        switch (self->action) {
                            case CLEAR: tr.clearSteps();  break;
                            case MUTE:  tr.toggleMute();  break;
                            case SOLO:  tr.toggleSolo();  break;
                            case NONE:
                            default:
                                *self->selected = index;
                                break;
                        }
                    }
                } else {
                    // step mode: instant ON, release OFF only if not edited
                    Sequencer& seq = (*self->tracks)[*self->selected];

                    if (value == 127) {
                        self->heldSteps[index] = true;
                        self->editedWhileHeld[index] = false;

                        if (!seq.getStepOn(index)) {
                            // OFF -> ON immediately
                            seq.setStepOn(index, true);
                            if (seq.getVelocity(index) == 0)
                                seq.setVelocity(index, kVelLevels[0]);
                            self->pendingOff[index] = false;
                        } else {
                            // ON -> pending off on release
                            self->pendingOff[index] = true;
                        }
                    }
                    else if (value == 0) {
                        self->heldSteps[index] = false;

                        if (self->pendingOff[index]) {
                            if (!self->editedWhileHeld[index]) {
                                seq.setStepOn(index, false);
                            }
                            self->pendingOff[index] = false;
                        }

                        self->editedWhileHeld[index] = false;
                    }
                }

                pkt = MIDIPacketNext(pkt);
                continue;
            }

            // If we didn't handle the CC, just fall through.
        }

        pkt = MIDIPacketNext(pkt);
    }
}






void MidiInterface::handleUserButton(int cc) {
    // Intentionally unassigned for now.
    // Put future behaviors here (pattern up/down, page left/right, etc.)
    (void)cc;
}

void MidiInterface::applyVelocityDeltaToHeld(int delta) {
    if (!tracks || !selected) return;

    Sequencer& seq = (*tracks)[*selected];

    for (int s = 0; s < 16; ++s) {
        if (heldSteps[s]) {
            seq.changeVelocity(s, delta);
        }
    }
}

int MidiInterface::clampVelLevel(int currentVel, int dir) const {
    // Map currentVel to an index 0..2, then move by dir and clamp (no wrap)
    int idx = 0;

    if (currentVel <= 0) idx = 0;
    else if (currentVel <= kVelLevels[0]) idx = 0;
    else if (currentVel <= kVelLevels[1]) idx = 1;
    else idx = 2;

    idx += dir;
    if (idx < 0) idx = 0;
    if (idx > 2) idx = 2;

    return kVelLevels[idx];
}

void MidiInterface::applyVelLevelToHeld(int dir) {
    if (!tracks || !selected) return;

    Sequencer& seq = (*tracks)[*selected];

    for (int s = 0; s < 16; ++s) {
        if (!heldSteps[s]) continue;
        editedWhileHeld[s] = true;

        // If step is off, turn it on at LOW so you get immediate LED feedback
        if (!seq.getStepOn(s)) {
            seq.setStepOn(s, true);
            seq.setVelocity(s, kVelLevels[0]);
            continue;
        }

        int curVel = seq.getVelocity(s);
        int next = clampVelLevel(curVel, dir);
        seq.setVelocity(s, next);
    }
}


