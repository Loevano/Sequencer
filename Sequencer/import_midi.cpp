#include "import_midi.hpp"
#include <algorithm>
#include <iostream>

// ==================================================
// Construction / Destruction
// ==================================================

MidiInterface::MidiInterface(std::vector<Sequencer>* seqs,
                             int* currentSeq,
                             bool* bank)
: sequences(seqs),
  currentSequence(currentSeq),
  bankMode(bank),
  currentAction(NONE)
{
    lastBlinkTime = std::chrono::steady_clock::now();
}

MidiInterface::~MidiInterface() {
    if (inputPort)  MIDIPortDispose(inputPort);
    if (outputPort) MIDIPortDispose(outputPort);
    if (midiClient) MIDIClientDispose(midiClient);
}

// ==================================================
// Initialization
// ==================================================

bool MidiInterface::initialize() {
    OSStatus result;

    // Create MIDI client
    result = MIDIClientCreate(CFSTR("MidiClient"), nullptr, nullptr, &midiClient);
    if (result != noErr) {
        std::cerr << "Failed to create MIDI client\n";
        return false;
    }

    // Create input port
    result = MIDIInputPortCreate(midiClient,
                                 CFSTR("InputPort"),
                                 midiReadCallback,
                                 this,
                                 &inputPort);
    if (result != noErr) {
        std::cerr << "Failed to create input port\n";
        return false;
    }

    // Create output port
    result = MIDIOutputPortCreate(midiClient,
                                  CFSTR("OutputPort"),
                                  &outputPort);
    if (result != noErr) {
        std::cerr << "Failed to create output port\n";
        return false;
    }

    // Connect all MIDI sources
    ItemCount sourceCount = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < sourceCount; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        MIDIPortConnectSource(inputPort, src, nullptr);

        CFStringRef nameRef = nullptr;
        MIDIObjectGetStringProperty(src, kMIDIPropertyName, &nameRef);

        char name[128] = {0};
        if (nameRef)
            CFStringGetCString(nameRef, name, 128, kCFStringEncodingUTF8);

        std::cout << "Connected to MIDI source: "
                  << (nameRef ? name : "Unknown") << "\n";
    }

    return true;
}

// ==================================================
// LED Helpers
// ==================================================

void MidiInterface::setLedState(int cc, bool on) {
    sendLedFeedback(cc, on ? 127 : 0);
}

void MidiInterface::sendLedFeedback(int cc, int value) {
    if (!outputPort) return;

    ItemCount destCount = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < destCount; ++i) {
        MIDIEndpointRef dest = MIDIGetDestination(i);

        UInt8 data[3] = { 0xB0, UInt8(cc), UInt8(value) };

        MIDIPacketList packetList;
        MIDIPacket* packet = MIDIPacketListInit(&packetList);
        MIDIPacketListAdd(&packetList,
                          sizeof(packetList),
                          packet,
                          0,
                          3,
                          data);

        MIDISend(outputPort, dest, &packetList);
    }
}

// ==================================================
// LED Update Logic
// ==================================================

void MidiInterface::updateSequencerLeds(bool bankModeActive, int baseCC) {
    // --- Blink timing ---
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastBlinkTime).count() >= blinkIntervalMs)
    {
        blinkFlag = !blinkFlag;
        lastBlinkTime = now;
    }

    // --------------------------------------------------
    // BANK MODE (TRACK VIEW)
    // --------------------------------------------------
    if (bankModeActive) {
        for (int track = 0; track < sequences->size(); ++track) {
            const Sequencer& seq = (*sequences)[track];
            int ledValue = 0;

            if (currentAction == CLEAR) {
                ledValue = seq.hasAnyActiveSteps()
                           ? (blinkFlag ? 127 : 0)
                           : 0;
            }
            else if (currentAction == MUTE) {
                ledValue = seq.isMuted(track)
                           ? (blinkFlag ? 127 : 0)
                           : 127;
            }
            else if (currentAction == SOLO) {
                ledValue = seq.isSoloed(track)
                           ? 127
                           : (blinkFlag ? 127 : 0);
            }
            else {
                // Normal bank view
                ledValue = seq.hasAnyActiveSteps() ? 127 : 0;

                // Selected track override
                if (track == *currentSequence)
                    ledValue = 74;
            }

            sendLedFeedback(stepCCs[track], ledValue);
        }
        return;
    }

    // --------------------------------------------------
    // NORMAL MODE (STEP VIEW)
    // --------------------------------------------------
    const Sequencer& seq = (*sequences)[*currentSequence];
    const int numSteps = seq.getNumSteps();
    const int currentStep = seq.getCurrentStep();

    for (int step = 0; step < numSteps; ++step) {
        int ledValue = seq.getStepState(step) ? 127 : 0;
        if (step == currentStep)
            ledValue = 74;

        sendLedFeedback(baseCC + step, ledValue);
    }
}

void MidiInterface::updateMenuLeds() {
    const std::vector<int> menuCCs = {53, 54, 55, 56};

    for (int cc : menuCCs) {
        int ledValue = 0;

        if (cc == 53 && *bankMode)              ledValue = 127;
        else if (cc == 56 && currentAction == CLEAR) ledValue = 127;
        else if (cc == 54 && currentAction == MUTE)  ledValue = 127;
        else if (cc == 55 && currentAction == SOLO)  ledValue = 127;

        sendLedFeedback(cc, ledValue);
    }
}

// ==================================================
// CoreMIDI Callback
// ==================================================

void MidiInterface::midiReadCallback(const MIDIPacketList* pktlist,
                                     void* readProcRefCon,
                                     void*)
{
    MidiInterface* self =
        static_cast<MidiInterface*>(readProcRefCon);

    const MIDIPacket* packet = &pktlist->packet[0];

    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        const UInt8* data = packet->data;

        if ((data[0] & 0xF0) == 0xB0) { // CC message
            int cc    = data[1];
            int value = data[2];

            // --------------------------------
            // BANK BUTTON (CC53, momentary)
            // --------------------------------
            if (cc == 53) {
                *self->bankMode = (value == 127);

                if (value == 0)
                    self->currentAction = NONE;

                self->updateSequencerLeds(*self->bankMode);
                self->updateMenuLeds();
            }

            // --------------------------------
            // ACTION BUTTONS (latched)
            // --------------------------------
            if (*self->bankMode && value == 127) {
                TrackAction requested = NONE;

                if (cc == 56) requested = CLEAR;
                else if (cc == 54) requested = MUTE;
                else if (cc == 55) requested = SOLO;

                if (requested != NONE) {
                    self->currentAction =
                        (self->currentAction == requested)
                        ? NONE
                        : requested;

                    self->updateSequencerLeds(*self->bankMode);
                    self->updateMenuLeds();
                }
            }

            // --------------------------------
            // STEP / TRACK BUTTONS (CC33–48)
            // --------------------------------
            auto it = std::find(self->stepCCs.begin(),
                                self->stepCCs.end(),
                                cc);

            if (it != self->stepCCs.end() && value == 127) {
                int index = std::distance(self->stepCCs.begin(), it);

                if (*self->bankMode) {
                    // ACTION MODE
                    if (self->currentAction != NONE) {
                        Sequencer& seq = (*self->sequences)[index];

                        switch (self->currentAction) {
                            case CLEAR: seq.reset(); break;
                            case MUTE:  seq.toggleMute(index); break;
                            case SOLO:  seq.toggleSolo(index); break;
                            default: break;
                        }
                    }
                    // TRACK SELECT MODE
                    else {
                        *self->currentSequence = index;
                        std::cout << "Selected track " << index << "\n";
                    }
                }
                // NORMAL STEP MODE
                else {
                    (*self->sequences)[*self->currentSequence]
                        .toggleStep(index);
                }
            }
        }

        packet = MIDIPacketNext(packet);
    }
}
