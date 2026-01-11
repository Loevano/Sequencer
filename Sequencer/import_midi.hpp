#pragma once
#include <CoreMIDI/CoreMIDI.h>
#include <vector>
#include <chrono>
#include "sequencer.h"

/**
 * TrackAction
 * -----------
 * Defines the current BANK action mode.
 */
enum TrackAction {
    NONE,
    CLEAR,
    MUTE,
    SOLO
};

/**
 * MidiInterface
 * -------------
 * Translates MIDI input into sequencer actions
 * and sequencer state into LED feedback.
 */
class MidiInterface {
public:
    // --- Lifecycle ---
    MidiInterface(std::vector<Sequencer>* seqs,
                  int* currentSeq,
                  bool* bankMode);
    ~MidiInterface();

    bool initialize();

    // --- LED updates ---
    void updateSequencerLeds(bool bankModeActive, int baseCC = 33);
    void updateMenuLeds();

    // --- Action state ---
    TrackAction getCurrentAction() const { return currentAction; }
    void setCurrentAction(TrackAction action) { currentAction = action; }

private:
    // --- CoreMIDI ---
    MIDIPortRef   inputPort  = 0;
    MIDIPortRef   outputPort = 0;
    MIDIClientRef midiClient = 0;

    static void midiReadCallback(const MIDIPacketList*,
                                 void* readProcRefCon,
                                 void* srcConnRefCon);

    // --- External state ---
    std::vector<Sequencer>* sequences;
    int*  currentSequence;
    bool* bankMode;

    // --- UI state ---
    TrackAction currentAction = NONE;

    // --- LED helpers ---
    void sendLedFeedback(int cc, int value);
    void setLedState(int cc, bool on);

    // --- CC mapping ---
    const std::vector<int> stepCCs = {
        33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48
    };

    // --- Blink state ---
    bool blinkFlag = false;
    int blinkIntervalMs = 200;
    std::chrono::steady_clock::time_point lastBlinkTime;
};
