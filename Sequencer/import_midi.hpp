#pragma once
#include <CoreMIDI/CoreMIDI.h>
#include <vector>
#include <iostream>
#include "sequencer.h"

class MidiInterface {
public:
    MidiInterface(std::vector<Sequencer>* seqs, int* currentSeq, bool* bankMode);
    ~MidiInterface();

    bool initialize();

    // LED feedback functions
    void setLedState(int cc, bool state);
    void sendLedFeedback(int cc, int value);
    void updateSequencerLeds(const Sequencer& seq, bool bankModeActive, int baseCC = 33);

private:
    MIDIPortRef inputPort;
    MIDIPortRef outputPort;
    MIDIClientRef midiClient;

    std::vector<Sequencer>* sequences;
    int* currentSequence; // index of the active sequence
    bool* bankMode;       // true if CC53 held

    static void midiReadCallback(const MIDIPacketList* pktlist, void* readProcRefCon, void* srcConnRefCon);

    // CC mapping for 16 steps
    const std::vector<int> stepCCs = {
        33, 34, 35, 36, 37, 38, 39, 40,
        41, 42, 43, 44, 45, 46, 47, 48
    };
    const int bankCC = 53; // momentary button for bank selection
};
