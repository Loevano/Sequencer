#include "sequencer.h"

Sequencer::Sequencer(int steps) : currentStep(0), numSteps(steps), sequence(steps, false) {}

void Sequencer::setStepState(int step, bool state) {
    if (step >= 0 && step < numSteps) {
        sequence[step] = state;
    }
}

int Sequencer::getNumSteps() const { return numSteps; }
int Sequencer::getCurrentStep() const {return currentStep;}

bool Sequencer::getStepState(int step) const {
    if (step >= 0 && step < numSteps)
        return sequence[step];
    return false;
}


void Sequencer::toggleStep(int step) {
    if (step >= 0 && step < numSteps)
        sequence[step] = !sequence[step];
}

void Sequencer::stepForward() {
    currentStep = (currentStep + 1) % numSteps;
    if (sequence[currentStep]) {
        std::cout << "Step " << currentStep << " triggered!\n";
    }
}

void Sequencer::printSequence() const {
    for (int i = 0; i < numSteps; ++i) {
        std::cout << (sequence[i] ? "[X]" : "[ ]");
    }
    std::cout << "\n";
}

void Sequencer::reset() {
    currentStep = 0;
    std::fill(sequence.begin(), sequence.end(), false);
}
