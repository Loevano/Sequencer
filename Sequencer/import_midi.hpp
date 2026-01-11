// ========================= import_midi.hpp =========================
#pragma once
#include <CoreMIDI/CoreMIDI.h>

#include <vector>
#include <chrono>
#include <string_view>

#include "sequencer.h"

enum Action {
    NONE,
    CLEAR,
    MUTE,
    SOLO
};

class MidiInterface {
public:
    MidiInterface(std::vector<Sequencer>* tracks,
                  int* selectedTrack,
                  bool* bankMode,
                  int* globalStep);

    ~MidiInterface();

    bool initialize();
    void tickStep(int step);

    // LED updates
    void updateStepLeds(int baseCC = 33);
    void updateBankLeds();
    void updateMenuLeds();

    bool isUsingMidiClock() const { return useMidiClock; }

private:
    // -------- CoreMIDI --------
    MIDIPortRef     inputPort  = 0;
    MIDIPortRef     outputPort = 0;
    MIDIClientRef   client     = 0;
    MIDIEndpointRef virtualSource = 0; // OUT to Ableton (Ableton sees as MIDI IN)
    MIDIEndpointRef virtualDest   = 0; // IN from Ableton  (Ableton sees as MIDI OUT)

    static void midiCallback(const MIDIPacketList*, void* refCon, void* srcConnRefCon);

    // -------- Realtime MIDI note output (to Ableton) --------
    int baseNote = 36;     // C1
    int lastTickStep = -1; // previous step we emitted (for note-offs)

    void sendMsg3(UInt8 status, UInt8 data1, UInt8 data2);
    void sendCC(int cc, int value);
    void sendNoteOn(int note, int vel);
    void sendNoteOff(int note);
    void allNotesOff();

    int trackToNote(int trackIndex) const { return baseNote + trackIndex; }

    bool anyTrackSoloed() const;
    bool trackAudible(const Sequencer& tr) const;

    // -------- External state --------
    std::vector<Sequencer>* tracks = nullptr;
    int*  selected = nullptr;
    bool* bank = nullptr;
    int*  playStep = nullptr; // global playhead step

    // -------- Track velocity scaling (pots CC1..16) --------
    int trackVelScale[16] = {
        127,127,127,127,127,127,127,127,
        127,127,127,127,127,127,127,127
    }; // 0..127 per track

    // -------- UI state --------
    Action action = NONE;

    const std::vector<int> stepCCs = {
        33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48
    };

    // Blink timing
    bool blinkOn = false;
    int  blinkIntervalMs = 200;
    std::chrono::steady_clock::time_point lastBlink = std::chrono::steady_clock::now();

    // Launch Control XL LED “string -> byte value”
    int  lcxlLedValue(std::string_view spec) const;
    void setLed(int cc, std::string_view spec);

    void handleUserButton(int cc);

    // Held step selection (pads 33–48) for velocity editing
    bool heldSteps[16] = { false };

    static constexpr int kVelLevels[3] = { 32, 80, 120 };
    int  clampVelLevel(int currentVel, int dir) const; // dir = +1 / -1
    void applyVelLevelToHeld(int dir);

    bool pendingOff[16] = { false };
    bool editedWhileHeld[16] = { false };

    int    heldStateCc = -1;              // which state button is currently held (54/55/56), or -1
    Action heldStateAction = NONE;        // which action we entered on press down
    bool   soloClearedDuringHold = false; // set true when we clear solos while SOLO is held

    bool exitStateOnRelease = false;
    bool soloButtonHeld = false;

    // -------- Sync / transport from MIDI clock (Ableton) --------
    bool useMidiClock = true;
    bool transportRunning = false;
    int  midiClockPulses = 0;

    static constexpr int kPulsesPerQuarter = 24;
    static constexpr int kPulsesPer16th    = 6;
};
