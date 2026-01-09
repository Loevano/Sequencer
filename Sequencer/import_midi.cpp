#include "import_midi.hpp"
#include <algorithm>

MidiInterface::MidiInterface(Sequencer* seq)
    : midiClient(0), inputPort(0), outputPort(0), sequencer(seq) {}


MidiInterface::~MidiInterface() {
    if (inputPort) MIDIPortDispose(inputPort);
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

    // Create output port for LED feedback
    result = MIDIOutputPortCreate(midiClient, CFSTR("OutputPort"), &outputPort);
    if (result != noErr) { std::cerr << "Failed to create output port\n"; return false; }

    // Connect all sources
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

            // Only handle CC messages
            if ((status & 0xF0) == 0xB0 && self->sequencer) {
                // Check if this CC is assigned to a step
                auto it = std::find(self->stepCCs.begin(), self->stepCCs.end(), cc);
                if (it != self->stepCCs.end()) {
                    int stepIndex = std::distance(self->stepCCs.begin(), it);

                    bool isOn = (value > 0);
                    self->sequencer->setStepState(stepIndex, isOn);

                    // Reflect LED on the same CC
                    self->setLedState(cc, isOn);

                    // Debug output
                    std::cout << "MIDI CC " << int(cc) << " value " << int(value)
                              << " → step " << stepIndex
                              << " LED " << (isOn ? "ON" : "OFF") << "\n";
                }
            }
        }

        packet = MIDIPacketNext(packet);
    }
}

void MidiInterface::readMidi() {
    // CoreMIDI calls the callback automatically
    
}

// Boolean wrapper for step LEDs
void MidiInterface::setLedState(int cc, bool state) {
    int ledValue = state ? 127 : 0;
    sendLedFeedback(cc, ledValue);
}

// Low-level MIDI CC sending
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

// Update LEDs for all steps and highlight current step
void MidiInterface::updateSequencerLeds(const Sequencer& seq, int baseCC) {
    int numSteps = seq.getNumSteps();
    int current = seq.getCurrentStep();

    for (int step = 0; step < numSteps; ++step) {
        int ledValue = seq.getStepState(step) ? 127 : 0; // step on/off
        if (step == current) ledValue = 74;              // highlight current step
        sendLedFeedback(baseCC + step, ledValue);
    }
}
