#pragma once
#include <vector>
#include <iostream>

class Sequencer {
public:
    Sequencer(int steps = 16);

    void toggleStep(int step);
    void setStepState(int step, bool state);
    void stepForward();
    void printSequence() const;
    void reset();


    
    // Track state setters
    void setMuted(int track, bool state);
    void setSoloed(int track, bool state);
    void toggleMute(int track);
    void toggleSolo(int track);
    
    // Track state getters
    bool isMuted(int track) const;
    bool isSoloed(int track) const;
    bool hasAnyStepsOn(int track) const;
    bool hasAnyActiveSteps() const;
    
    int getNumSteps() const { return numSteps; }
    int getCurrentStep() const { return currentStep; }
    bool getStepState(int step) const {
        if (step >= 0 && step < numSteps) return sequence[step];
        return false;
    }

private:
    int currentStep;
    int numSteps;
    std::vector<bool> sequence;
    
    struct TrackState {
        bool muted = false;
        bool soloed = false;
        bool hasContent = false;
    };
    std::vector<TrackState> tracks; // size = numSteps
    
};

