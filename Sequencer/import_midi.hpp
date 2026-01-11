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

    // LED updates
    void updateStepLeds(int baseCC = 33);
    void updateBankLeds();
    void updateMenuLeds();

private:
    // CoreMIDI
    MIDIPortRef   inputPort  = 0;
    MIDIPortRef   outputPort = 0;
    MIDIClientRef client     = 0;

    static void midiCallback(const MIDIPacketList*, void* refCon, void*);

    // External state
    std::vector<Sequencer>* tracks = nullptr;
    int* selected = nullptr;
    bool* bank = nullptr;
    int* playStep = nullptr; // global playhead step

    // UI state
    Action action = NONE;

    // CC mapping (track/step buttons)
    const std::vector<int> stepCCs = {
        33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48
    };

    // Blink timing
    bool blinkOn = false;
    int blinkIntervalMs = 200;
    std::chrono::steady_clock::time_point lastBlink = std::chrono::steady_clock::now();

    // Sending helpers
    void sendCC(int cc, int value);

    // Launch Control XL LED “string -> byte value”
    int  lcxlLedValue(std::string_view spec) const;
    void setLed(int cc, std::string_view spec);
    
    void handleUserButton(int cc);
    
    // Held step selection (pads 33–48) for velocity editing
    bool heldSteps[16] = { false };

    // Velocity edit settings
    int velocityStep = 5; // change per CC49/50 press; adjust to taste

    void applyVelocityDeltaToHeld(int delta);
    
    static constexpr int kVelLevels[3] = { 32, 80, 120 };

    int clampVelLevel(int currentVel, int dir) const; // dir = +1 (up) or -1 (down)
    void applyVelLevelToHeld(int dir);
    
    bool pendingOff[16] = { false };   // step was ON and user pressed it -> maybe turn off on release
    bool editedWhileHeld[16] = { false }; // velocity changed while holding this step
    
    int   heldStateCc = -1;              // which state button is currently held (54/55/56), or -1
    Action heldStateAction = NONE;       // which action we entered on press down
    bool  soloClearedDuringHold = false; // set true when we clear solos while SOLO is held

    bool  exitStateOnRelease = false;    // <-- ADD THIS
    bool soloButtonHeld = false;   // CC55 physical hold state (only relevant while BANK is held)

};
