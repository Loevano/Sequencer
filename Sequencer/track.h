#pragma once
#include <vector>
#include <algorithm>

class Track {
public:
    explicit Track(int steps = 16, int midiNote = 36)
        : steps(steps, false), midiNote(midiNote) {}

    // --- Step control ---
    void toggleStep(int step) {
        if (inRange(step)) steps[step] = !steps[step];
    }

    bool stepOn(int step) const {
        return inRange(step) ? steps[step] : false;
    }

    void clear() {
        std::fill(steps.begin(), steps.end(), false);
    }

    bool hasAnySteps() const {
        return std::any_of(steps.begin(), steps.end(),
                           [](bool s){ return s; });
    }

    // --- Track state ---
    void toggleMute() { muted = !muted; }
    void toggleSolo() { soloed = !soloed; }

    bool isMuted() const { return muted; }
    bool isSoloed() const { return soloed; }

    int getMidiNote() const { return midiNote; }
    int getNumSteps() const { return (int)steps.size(); }

private:
    std::vector<bool> steps;
    int midiNote;
    bool muted  = false;
    bool soloed = false;

    bool inRange(int s) const {
        return s >= 0 && s < steps.size();
    }
};
