#include "import_midi.hpp"
#include <algorithm>

MidiInterface::MidiInterface(std::vector<Sequencer>* seqs, int* currentSeq, bool* bank)
    : inputPort(0), outputPort(0), midiClient(0),
      sequences(seqs), currentSequence(currentSeq), bankMode(bank) {}

MidiInterface::~MidiInterface() {
    if (inputPort)  MIDIPortDispose(inputPort);
    if (outputPort) MIDIPortDispose(outputPort);
    if (midiClient) MIDIClientDispose(midiClient);
}

bool MidiInterface::initialize() {
    OSStatus result;

    // Create MIDI client
    result = MIDIClientCreate(CFSTR("MidiClient"), nullptr, nullptr, &midiClient);
    if (result != noErr) { std::cerr << "Failed to create MIDI client\n"; return false; }

    // Create input port
    result = MIDIInputPortCreate(midiClient, CFSTR("InputPort"), midiReadCallback, this, &inputPort);
    if (result != noErr) { std::cerr << "Failed to create input port\n"; return false; }

    // Create output port
    result = MIDIOutputPortCreate(midiClient, CFSTR("OutputPort"), &outputPort);
    if (result != noErr) { std::cerr << "Failed to create output port\n"; return false; }

    // Connect all MIDI sources
    ItemCount sourceCount = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < sourceCount; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        MIDIPortConnectSource(inputPort, src, nullptr);

        CFStringRef nameRef = nullptr;
        MIDIObjectGetStringProperty(src, kMIDIPropertyName, &nameRef);
        char name[128] = {0};
        if (nameRef) CFStringGetCString(nameRef, name, 128, kCFStringEncodingUTF8);
        std::cout << "Connected to MIDI source: " << (nameRef ? name : "Unknown") << "\n";
    }

    return true;
}

// Set LED on/off
void MidiInterface::setLedState(int cc, bool state) {
    int ledValue = state ? 127 : 0;
    sendLedFeedback(cc, ledValue);
}

// Send raw LED CC
void MidiInterface::sendLedFeedback(int cc, int value) {
    if (!outputPort) return;

    ItemCount destCount = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < destCount; ++i) {
        MIDIEndpointRef dest = MIDIGetDestination(i);

        UInt8 packetData[3] = {0xB0, UInt8(cc), UInt8(value)};
        MIDIPacketList packetList;
        MIDIPacket* packet = MIDIPacketListInit(&packetList);
        packet = MIDIPacketListAdd(&packetList, sizeof(packetList), packet, 0, 3, packetData);

        MIDISend(outputPort, dest, &packetList);
    }
}

// Update LEDs for a sequencer (bank or normal mode)
void MidiInterface::updateSequencerLeds(bool bankModeActive, int baseCC)
{
    // Blink logic
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastBlinkTime).count() >= blinkIntervalMs)
    {
        blinkFlag = !blinkFlag;
        lastBlinkTime = now;
    }

    if (bankModeActive && currentAction == CLEAR) {
        for (int track = 0; track < sequences->size(); ++track) {
            const Sequencer& seq = (*sequences)[track];

            int ledValue = seq.hasAnyActiveSteps() ? (blinkFlag ? 127 : 0) : 0;

            sendLedFeedback(baseCC + track, ledValue);
        }
        return; // done
    }

    // For other modes, still just show current sequence
    const Sequencer& seq = (*sequences)[*currentSequence];
    const int numSteps = seq.getNumSteps();
    const int currentStep = seq.getCurrentStep();

    for (int step = 0; step < numSteps; ++step) {
        int ledValue = 0;

        if (bankModeActive) {
            switch (currentAction) {
                case MUTE:
                    ledValue = seq.isMuted(step) ? (blinkFlag ? 127 : 0) : 127;
                    break;
                case SOLO:
                    ledValue = seq.isSoloed(step) ? 127 : (blinkFlag ? 127 : 0);
                    break;
                default:
                    ledValue = (step == *currentSequence) ? 70 : 0;
            }
        } else {
            ledValue = seq.getStepState(step) ? 127 : 0;
            if (step == currentStep) ledValue = 74;
        }

        sendLedFeedback(baseCC + step, ledValue);
    }
}





// CoreMIDI callback
void MidiInterface::midiReadCallback(const MIDIPacketList* pktlist,
                                    void* readProcRefCon,
                                    void* /*srcConnRefCon*/)
{
    MidiInterface* self = static_cast<MidiInterface*>(readProcRefCon);

    const MIDIPacket* packet = &pktlist->packet[0];

    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        const UInt8* data = packet->data;
        UInt8 status = data[0] & 0xF0;

        if (status == 0xB0) { // CC message
            int cc = data[1];
            int value = data[2];

            // -----------------------------
            // CC53 → BANK MODE (momentary)
            // -----------------------------
            if (cc == 53) {
                *self->bankMode = (value == 127);

                // Release CC53 → exit all actions
                if (value == 0) {
                    self->currentAction = NONE;
                }
            }

            // --------------------------------
            // ACTION BUTTONS (LATCHED)
            // --------------------------------
            if (*self->bankMode && value == 127) {

                TrackAction requestedAction = NONE;
                if (cc == 56) requestedAction = CLEAR;
                else if (cc == 54) requestedAction = MUTE;
                else if (cc == 55) requestedAction = SOLO;

                if (requestedAction != NONE) {
                    // Toggle behavior
                    if (self->currentAction == requestedAction) {
                        self->currentAction = NONE;   // exit state
                    } else {
                        self->currentAction = requestedAction; // enter state
                    }
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

                // ----- BANK MODE -----
                if (*self->bankMode) {

                    // ACTION MODE
                    if (self->currentAction != NONE) {

                        switch (self->currentAction) {
                            case CLEAR:
                                (*self->sequences)[index].reset();
                                std::cout << "Cleared track " << index << "\n";
                                break;

                            case MUTE:
                                // future
                                break;

                            case SOLO:
                                // future
                                break;

                            default:
                                break;
                        }
                    }
                    // TRACK SELECTION MODE
                    else {
                        *self->currentSequence = index;
                        std::cout << "Selected track " << index << "\n";
                    }
                }

                // ----- NORMAL MODE -----
                else {
                    (*self->sequences)[*self->currentSequence].toggleStep(index);
                }
            }
        }

        packet = MIDIPacketNext(packet);
    }
}


void MidiInterface::updateMenuLeds() {
    const std::vector<int> menuCCs = {53, 54, 55, 56};

    for (int cc : menuCCs) {
        int ledValue = 0;

        if (cc == 53 && *bankMode) ledValue = 127;
        else if (cc == 56 && currentAction == CLEAR) ledValue = 127;
        else if (cc == 54 && currentAction == MUTE) ledValue = 127;
        else if (cc == 55 && currentAction == SOLO) ledValue = 127;

        sendLedFeedback(cc, ledValue);
    }
}


TrackAction MidiInterface::getCurrentAction() const {
    return currentAction;
}
void MidiInterface::setCurrentAction(TrackAction action) {
    currentAction = action;
}
