#include "sequencer.h"

// --------------------------------------------------
// Construction
// --------------------------------------------------

Sequencer::Sequencer(int steps)
    : currentStep(0),
      numSteps(steps),
      sequence(steps, false),
      tracks(steps)
{}

// --------------------------------------------------
// Core sequencing
// --------------------------------------------------

void Sequencer::toggleStep(int step) {
    if (step < 0 || step >= numSteps) return;
    sequence[step] = !sequence[step];
    tracks[step].hasContent = sequence[step];
}

void Sequencer::setStepState(int step, bool state) {
    if (step < 0 || step >= numSteps) return;
    sequence[step] = state;
    tracks[step].hasContent = state;
}

void Sequencer::stepForward() {
    currentStep = (currentStep + 1) % numSteps;
}

void Sequencer::reset() {
    std::fill(sequence.begin(), sequence.end(), false);
    for (auto& t : tracks) t.hasContent = false;
    currentStep = 0;
}

void Sequencer::printSequence() const {
    for (int i = 0; i < numSteps; ++i)
        std::cout << (sequence[i] ? "X" : "-")
                  << (i == currentStep ? "|" : " ");
    std::cout << "\n";
}

// --------------------------------------------------
// Track state mutation
// --------------------------------------------------

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

// --------------------------------------------------
// Queries
// --------------------------------------------------

bool Sequencer::isMuted(int track) const {
    return (track >= 0 && track < tracks.size()) ? tracks[track].muted : false;
}

bool Sequencer::isSoloed(int track) const {
    return (track >= 0 && track < tracks.size()) ? tracks[track].soloed : false;
}

bool Sequencer::hasAnyStepsOn(int track) const {
    return (track >= 0 && track < tracks.size()) ? tracks[track].hasContent : false;
}

bool Sequencer::hasAnyActiveSteps() const {
    return std::any_of(sequence.begin(), sequence.end(),
                       [](bool s) { return s; });
}
