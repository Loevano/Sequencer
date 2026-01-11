#pragma once
#include <vector>
#include <iostream>
#include <algorithm>

/**
 * Sequencer
 * ----------
 * Holds step data and per-track state (mute / solo / content).
 * Does NOT know anything about MIDI or UI.
 */
class Sequencer {
public:
    // --- Construction ---
    explicit Sequencer(int steps = 16);

    // --- Core sequencing ---
    void toggleStep(int step);
    void setStepState(int step, bool state);
    void stepForward();
    void reset();
    void printSequence() const;

    // --- Track state control ---
    void setMuted(int track, bool state);
    void setSoloed(int track, bool state);
    void toggleMute(int track);
    void toggleSolo(int track);

    // --- Queries ---
    bool isMuted(int track) const;
    bool isSoloed(int track) const;
    bool hasAnyStepsOn(int track) const;
    bool hasAnyActiveSteps() const;

    // --- Lightweight getters (inline) ---
    int  getNumSteps()    const { return numSteps; }
    int  getCurrentStep() const { return currentStep; }

    bool getStepState(int step) const {
        return (step >= 0 && step < numSteps) ? sequence[step] : false;
    }

private:
    // --- Internal state ---
    int currentStep = 0;
    int numSteps = 0;

    std::vector<bool> sequence;

    struct TrackState {
        bool muted = false;
        bool soloed = false;
        bool hasContent = false;
    };

    std::vector<TrackState> tracks;
};
