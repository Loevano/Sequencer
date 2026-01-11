#include "import_midi.hpp"
#include <algorithm>

MidiInterface::MidiInterface(std::vector<Sequencer>* seqs, int* currentSeq, bool* bank)
: inputPort(0),
  outputPort(0),
  midiClient(0),
  sequences(seqs),
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
    // Blink toggle
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastBlinkTime).count() >= blinkIntervalMs)
    {
        blinkFlag = !blinkFlag;
        lastBlinkTime = now;
    }

    if (bankModeActive) {
        for (int track = 0; track < sequences->size(); ++track) {
            const Sequencer& seq = (*sequences)[track];
            int ledValue = 0;

            // --- Action handling ---
            if (currentAction == CLEAR) {
                ledValue = seq.hasAnyActiveSteps() ? (blinkFlag ? 127 : 0) : 0;
            }
            // <-- THIS IS WHERE the MUTE logic goes
            else if (currentAction == MUTE) {
                ledValue = seq.isMuted(track) ? (blinkFlag ? 127 : 0) : 127;
            }
            else if (currentAction == SOLO) {
                ledValue = seq.isSoloed(track) ? 127 : (blinkFlag ? 127 : 0);
            }
            else {
                // No action → normal bank LEDs
                ledValue = seq.hasAnyActiveSteps() ? 127 : 0;

                // Selected track override
                if (track == *currentSequence) ledValue = 74;
            }

            sendLedFeedback(stepCCs[track], ledValue);
        }
        return; // done with bank mode
    }

    // --- NORMAL SEQUENCER MODE ---
    const Sequencer& seq = (*sequences)[*currentSequence];
    const int numSteps = seq.getNumSteps();
    const int currentStep = seq.getCurrentStep();

    for (int step = 0; step < numSteps; ++step) {
        int ledValue = seq.getStepState(step) ? 127 : 0;
        if (step == currentStep) ledValue = 74; // highlight current step
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

            // CC53 → BANK MODE (MOMENTARY)
            if (cc == 53) {
                *self->bankMode = (value == 127);

                if (value == 0) {
                    self->currentAction = NONE;
                }

                // FORCE refresh
                self->updateSequencerLeds(*self->bankMode);
                self->updateMenuLeds();
            }

            // ACTION BUTTONS (LATCHED)
            if (*self->bankMode && value == 127) {

                TrackAction requestedAction = NONE;
                if (cc == 56) requestedAction = CLEAR;
                else if (cc == 54) requestedAction = MUTE;
                else if (cc == 55) requestedAction = SOLO;

                if (requestedAction != NONE) {
                    if (self->currentAction == requestedAction)
                        self->currentAction = NONE;
                    else
                        self->currentAction = requestedAction;

                    // FORCE refresh
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

                // ----- BANK MODE -----
                if (*self->bankMode) {

                    // ACTION MODE
                    if (self->currentAction != NONE) {

                        switch (self->currentAction) {
                            case CLEAR: {
                                (*self->sequences)[index].reset();
                                std::cout << "Cleared track " << index << "\n";
                                break;
                            }

                            case MUTE: {
                                bool m = (*self->sequences)[index].isMuted(index);
                                (*self->sequences)[index].setMuted(index, !m);
                                break;
                            }
                            case SOLO: {
                                bool s = (*self->sequences)[index].isSoloed(index);
                                (*self->sequences)[index].setSoloed(index, !s);
                                break;
                            }

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
    // --- Menu buttons ---
    const std::vector<int> menuCCs = {53, 54, 55, 56};
    for (int cc : menuCCs) {
        int ledValue = 0;
        if (cc == 53 && *bankMode) ledValue = 127;       // bank button LED
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
