#include "import_midi.hpp"

MidiInterface::MidiInterface(Sequencer* seq) : midiClient(0), inputPort(0), outputPort(0), sequencer(seq) {}


MidiInterface::~MidiInterface() {
    if (inputPort) MIDIPortDispose(inputPort);
    if (midiClient) MIDIClientDispose(midiClient);
}

bool MidiInterface::initialize() {
    OSStatus result;

    // Create MIDI client
    result = MIDIClientCreate(CFSTR("MidiClient"), nullptr, nullptr, &midiClient);
    if (result != noErr) {
        std::cerr << "Error creating MIDI client\n";
        return false;
    }

    // Create input port
    result = MIDIInputPortCreate(midiClient, CFSTR("InputPort"), midiReadCallback, this, &inputPort);
    if (result != noErr) {
        std::cerr << "Error creating input port\n";
        return false;
    }
    
    result = MIDIOutputPortCreate(midiClient, CFSTR("OutputPort"), &outputPort);
    if (result != noErr) {
        std::cerr << "Error creating MIDI output port\n";
        return false;
    }

    // Connect all sources
    ItemCount sourceCount = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < sourceCount; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        result = MIDIPortConnectSource(inputPort, src, nullptr);

        CFStringRef nameRef = nullptr;
        MIDIObjectGetStringProperty(src, kMIDIPropertyName, &nameRef);
        char name[128] = {0};
        if (nameRef) CFStringGetCString(nameRef, name, 128, kCFStringEncodingUTF8);
        deviceNames.push_back(nameRef ? std::string(name) : "Unknown Device");

        std::cout << "Connected to MIDI source: " << deviceNames.back() << "\n";
    }
    
    ItemCount destCount = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < destCount; ++i) {
        MIDIEndpointRef dest = MIDIGetDestination(i);
        CFStringRef nameRef = nullptr;
        MIDIObjectGetStringProperty(dest, kMIDIPropertyName, &nameRef);
        char name[128] = {0};
        if (nameRef) CFStringGetCString(nameRef, name, 128, kCFStringEncodingUTF8);
        std::cout << "Connected to MIDI destination: " << name << "\n";
    }

    return true;
}

// Callback
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

            // Only handle MIDI CC
            if ((status & 0xF0) == 0xB0 && self->sequencer) {

                // map CC to sequencer step dynamically with +1 offset
                int step = (cc % self->sequencer->getNumSteps()) - 1;
                if (step < 0)
                    step = self->sequencer->getNumSteps() - 1;

                // set sequencer step
                bool isOn = (value > 0);
                self->sequencer->setStepState(step, isOn);

                // send LED feedback to mirror the CC
                self->setLedState(cc, isOn);

                // debug output
                std::cout << "MIDI CC " << int(cc) << " value " << int(value)
                          << " → step " << step
                          << " LED " << (isOn ? "ON" : "OFF") << "\n";
            }
        }

        packet = MIDIPacketNext(packet);
    }
}

void MidiInterface::readMidi() {
    // CoreMIDI calls the callback automatically
    
}

void MidiInterface::setLedState(int cc, bool state) {
    // map boolean to LED brightness: 127 = on, 0 = off
    int ledValue = state ? 127 : 0;
    sendLedFeedback(cc, ledValue); // call the low-level sending function

    // Optional debug
    std::cout << "LED CC " << cc << " set to " << ledValue << "\n";
}

void MidiInterface::sendLedFeedback(int cc, int value) {
    if (!outputPort) return; // make sure output port exists

    // Send to all connected destinations
    ItemCount destCount = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < destCount; ++i) {
        MIDIEndpointRef dest = MIDIGetDestination(i);

        UInt8 packetData[3];
        packetData[0] = 0xB0; // CC message, channel 1
        packetData[1] = cc;   // same CC as received
        packetData[2] = value; // brightness/color value

        MIDIPacketList packetList;
        MIDIPacket* packet = MIDIPacketListInit(&packetList);
        packet = MIDIPacketListAdd(&packetList, sizeof(packetList), packet, 0, 3, packetData);

        MIDISend(outputPort, dest, &packetList);
    }
}

void MidiInterface::updateSequencerLeds(const Sequencer& seq, int baseCC) {
    int numSteps = seq.getNumSteps();
    int current = seq.getCurrentStep();

    for (int step = 0; step < numSteps; ++step) {
        int ledValue = seq.getStepState(step) ? 127 : 0;  // use getter
        if (step == current)
            ledValue = 74; // highlight current step

        sendLedFeedback(baseCC + step, ledValue);
    }
}
