#include "sequencer.h"

Sequencer::Sequencer(int stepsCount)
: numSteps(stepsCount),
  stepVel(stepsCount, 0)
{}

void Sequencer::toggleStep(int step) {
    if (step < 0 || step >= numSteps) return;

    if (stepVel[step] == 0) {
        // default velocity when turning on
        stepVel[step] = 100;
    } else {
        stepVel[step] = 0;
    }
}

void Sequencer::setStepOn(int step, bool on) {
    if (step < 0 || step >= numSteps) return;

    if (on) {
        if (stepVel[step] == 0) stepVel[step] = 100;
    } else {
        stepVel[step] = 0;
    }
}

bool Sequencer::getStepOn(int step) const {
    if (step < 0 || step >= numSteps) return false;
    return stepVel[step] != 0;
}

void Sequencer::setVelocity(int step, int velocity) {
    if (step < 0 || step >= numSteps) return;

    // If step is currently off, we still store velocity but do not implicitly turn it on.
    // (You can change this if you want CC49/50 to also "arm" steps.)
    int v = clampVel(velocity);
    stepVel[step] = static_cast<uint8_t>(v);
}

int Sequencer::getVelocity(int step) const {
    if (step < 0 || step >= numSteps) return 0;
    return (int)stepVel[step];
}

void Sequencer::changeVelocity(int step, int delta) {
    if (step < 0 || step >= numSteps) return;

    int cur = (int)stepVel[step];
    if (cur == 0) {
        // If step is off, do nothing. (Alternative: set to default and adjust.)
        return;
    }
    int next = clampVel(cur + delta);
    stepVel[step] = static_cast<uint8_t>(next);
}

void Sequencer::clearSteps() {
    std::fill(stepVel.begin(), stepVel.end(), 0);
}

bool Sequencer::hasSteps() const {
    return std::any_of(stepVel.begin(), stepVel.end(), [](uint8_t v){ return v != 0; });
}

void Sequencer::toggleMute() { muted = !muted; }
void Sequencer::toggleSolo() { soloed = !soloed; }
bool Sequencer::isMuted()  const { return muted; }
bool Sequencer::isSoloed() const { return soloed; }
