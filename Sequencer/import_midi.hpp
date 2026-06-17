// ========================= import_midi.hpp =========================
#pragma once
#include <CoreMIDI/CoreMIDI.h>

#include <atomic>
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
    MidiInterface(std::vector<std::vector<Sequencer>>* allBanks,
                  bool* bankMode,
                  int* globalStep,
                  bool debugLogging = false);

    ~MidiInterface();

    bool initialize();
    void tickStep(int step);

    // LED updates
    void updateStepLeds(int baseCC = 33);
    void updateBankLeds();
    void updateMenuLeds();
    void updateRotaryLeds();

    bool isUsingMidiClock() const { return useMidiClock; }

private:
    static constexpr int kUserChannels = 16;

    // -------- CoreMIDI --------
    MIDIPortRef     inputPort  = 0;
    MIDIPortRef     outputPort = 0;
    MIDIClientRef   client     = 0;
    MIDIEndpointRef virtualSource = 0; // OUT to Ableton (Ableton sees as MIDI IN)
    MIDIEndpointRef virtualDest   = 0; // IN from Ableton  (Ableton sees as MIDI OUT)
    bool debugLogging = false;

    static void midiCallback(const MIDIPacketList*, void* refCon, void* srcConnRefCon);

    // -------- Realtime MIDI note output (to Ableton) --------
    int baseNote = 36;     // C1
    int lastTickStep = -1; // previous step we emitted (for note-offs)

    void sendMsg3(UInt8 status, UInt8 data1, UInt8 data2);
    void sendSysEx(const std::vector<UInt8>& data);
    void sendCC(int cc, int value);
    void sendNoteOn(int note, int vel, int channel);
    void sendNoteOff(int note, int channel);
    void allNotesOff();

    int trackToNote(int trackIndex) const { return baseNote + trackIndex; }

    bool anyTrackSoloed(const std::vector<Sequencer>& tracks) const;
    bool trackAudible(const Sequencer& tr, const std::vector<Sequencer>& tracks) const;
    bool anyChannelSoloed() const;
    bool channelAudible(int user) const;

    // -------- External state --------
    std::vector<std::vector<Sequencer>>* banks = nullptr;
    bool* bank = nullptr;
    int*  playStep = nullptr; // global playhead step
    int   activeUser = 0;
    int   selectedByUser[kUserChannels] = { 0 };
    bool  channelSelectHeld = false;
    bool  channelMuted[kUserChannels] = {};
    bool  channelSoloed[kUserChannels] = {};

    // -------- Track velocity scaling (pots CC1..16) --------
    int trackVelScale[kUserChannels][16] = {}; // 0..127 per track
    bool trackNoteOn[kUserChannels][16] = {};
    std::chrono::steady_clock::time_point rotaryLedUntil[kUserChannels][16] = {};
    bool rotaryLedShown[16] = {};

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
    static constexpr int kRotaryPulseMs = 90;

    // Launch Control XL LED “string -> byte value”
    int  lcxlLedValue(std::string_view spec) const;
    void setLed(int cc, std::string_view spec);
    void setRotaryLed(int index, std::string_view spec);
    void clearRotaryLeds();

    static constexpr int kVelLevels[3] = { 32, 80, 120 };
    int  defaultVelLevel = 2;
    int  defaultVelocity() const { return kVelLevels[defaultVelLevel]; }
    void adjustDefaultVelLevel(int dir);
    bool adjustHeldStepVelocities(int dir);

    bool pendingOff[16] = { false };
    bool stepHeld[16] = { false };
    bool stepEditedWhileHeld[16] = { false };

    int    heldStateCc = -1;              // which state button is currently held (54/55/56), or -1
    Action heldStateAction = NONE;        // which action we entered on press down
    bool   soloClearedDuringHold = false; // set true when we clear solos while SOLO is held

    bool exitStateOnRelease = false;
    bool soloButtonHeld = false;

    Action channelAction = NONE;
    int    heldChannelStateCc = -1;
    Action heldChannelStateAction = NONE;
    bool   exitChannelStateOnRelease = false;

    // -------- Sync / transport from MIDI clock (Ableton) --------
    bool useMidiClock = true;
    bool transportRunning = false;
    int  midiClockPulses = 0;
    int  clockStep = 0;
    std::atomic<int> swingPulses{0}; // -3=3/9, 0=straight, 3=9/3
    int  pendingSwingStep = -1;
    int  pendingSwingPulse = 0;
    int  earlySwingStep = -1;

    static constexpr int kPulsesPerQuarter = 24;
    static constexpr int kPulsesPer16th    = 6;
    static constexpr int kMaxSwingPulses   = 3;

    void resetClockState();
    void advanceMidiClockPulse();
    void scheduleOrPlayStep(int step);
    void playPendingSwingStepIfDue();
    void playEarlySwingStepIfDue();
    int  currentStepCount() const;
    void adjustSwingPulses(int dir);
};
