#pragma once
#include <CoreMIDI/CoreMIDI.h>
#include <iostream>
#include <string>
#include <vector>
#include "sequencer.h"

class MidiInterface {
public:
    MidiInterface(Sequencer* seq = nullptr); // optional sequencer reference
    ~MidiInterface();

    bool initialize();
    void readMidi(); // callback handles messages
    void setLedState(int cc, bool state);
    void sendLedFeedback(int cc, int value);
    
    void updateSequencerLeds(const Sequencer& seq, int baseCC = 33);
    


private:
    MIDIClientRef midiClient;
    MIDIPortRef inputPort;
    MIDIPortRef outputPort;
    std::vector<std::string> deviceNames;
    Sequencer* sequencer; // pointer to sequencer

    static void midiReadCallback(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon);
};
