#include "sequencer.h"

Sequencer::Sequencer(int steps)
    : currentStep(0), numSteps(steps), sequence(steps, false), tracks(steps) {}

void Sequencer::toggleStep(int step) {
    sequence[step] = !sequence[step];
    tracks[step].hasContent = sequence[step];
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

void Sequencer::setMuted(int track, bool state) {
    if (track >= 0 && track < tracks.size())
        tracks[track].muted = state;
}

void Sequencer::setSoloed(int track, bool state) {
    if (track >= 0 && track < tracks.size())
        tracks[track].soloed = state;
}

void Sequencer::toggleMute(int track) {
    if (track >= 0 && track < tracks.size())
        tracks[track].muted = !tracks[track].muted;
}

void Sequencer::toggleSolo(int track) {
    if (track >= 0 && track < tracks.size())
        tracks[track].soloed = !tracks[track].soloed;
}
bool Sequencer::hasAnyActiveSteps() const {
    return std::any_of(sequence.begin(), sequence.end(),
                       [](bool step) { return step; });
}

// Sequencer.cpp
bool Sequencer::hasAnyStepsOn(int track) const {
    if (track >= 0 && track < tracks.size())
        return tracks[track].hasContent;
    return false;
}

bool Sequencer::isSoloed(int track) const {
    if (track >= 0 && track < tracks.size())
        return tracks[track].soloed;
    return false;
}

bool Sequencer::isMuted(int track) const {
    if (track >= 0 && track < tracks.size())
        return tracks[track].muted;
    return false;
}
