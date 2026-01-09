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
    
// Setter en Getter
    int getNumSteps() const;// <-- add this getter
    int getCurrentStep() const;
    
    bool getStepState(int step) const;


private:
    int currentStep; // Current position of the sequencer.
    int numSteps; // Amount of steps in the sequencer.
    
    std::vector<bool> sequence;

};
