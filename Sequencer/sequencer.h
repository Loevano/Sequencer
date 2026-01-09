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
};

