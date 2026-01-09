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
void MidiInterface::updateSequencerLeds(const Sequencer& seq, bool bankModeActive, int baseCC) {
    int numSteps = seq.getNumSteps();
    int current = seq.getCurrentStep();

    for (int step = 0; step < numSteps; ++step) {
        int ledValue = 0;

        if (bankModeActive) {
            // Bank mode: show selected sequence
            if (step == *currentSequence) ledValue = 70;
            else ledValue = 0;
        } else {
            // Normal mode: show step on/off + current step
            ledValue = seq.getStepState(step) ? 127 : 0;
            if (step == current) ledValue = 74;
        }

        sendLedFeedback(baseCC + step, ledValue);
    }
}

// CoreMIDI callback
void MidiInterface::midiReadCallback(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon) {
    MidiInterface* self = static_cast<MidiInterface*>(readProcRefCon);

    const MIDIPacket* packet = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets && packet != nullptr; ++i) {
        UInt8* data = const_cast<UInt8*>(packet->data);
        UInt16 length = packet->length;

        if (length >= 3) {
            UInt8 status = data[0];
            UInt8 cc     = data[1];
            UInt8 value  = data[2];

            if ((status & 0xF0) == 0xB0 && self->sequences) {

                // Handle bank modifier CC53
                if (cc == self->bankCC) {
                    *self->bankMode = (value > 0);
                }

                // Step CCs
                auto it = std::find(self->stepCCs.begin(), self->stepCCs.end(), cc);
                if (it != self->stepCCs.end()) {
                    int stepIndex = std::distance(self->stepCCs.begin(), it);

                    if (*self->bankMode) {
                        // Bank selection
                        if (value == 127) { // only act on press
                            *self->currentSequence = stepIndex;
                            std::cout << "Switched to sequence " << *self->currentSequence << "\n";
                        }
                    } else {
                        // Normal mode: toggle step only on press
                        if (value == 127) {
                            (*self->sequences)[*self->currentSequence].toggleStep(stepIndex);
                            self->setLedState(cc, (*self->sequences)[*self->currentSequence].getStepState(stepIndex));
                        }
                    }
                }
            }
        }

        packet = MIDIPacketNext(packet);
    }
}
