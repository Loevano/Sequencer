#include "sequencer.h"

Sequencer::Sequencer(int steps)
    : currentStep(0), numSteps(steps), sequence(steps, false) {}

void Sequencer::toggleStep(int step) {
    if (step >= 0 && step < numSteps)
        sequence[step] = !sequence[step];
}

void Sequencer::setStepState(int step, bool state) {
    if (step >= 0 && step < numSteps)
        sequence[step] = state;
}

void Sequencer::stepForward() {
    currentStep = (currentStep + 1) % numSteps;
}

void Sequencer::printSequence() const {
    for (int i = 0; i < numSteps; ++i)
        std::cout << (sequence[i] ? "X" : "-") << (i == currentStep ? "|" : " ");
    std::cout << "\n";
}

void Sequencer::reset() {
    std::fill(sequence.begin(), sequence.end(), false);
    currentStep = 0;
}
