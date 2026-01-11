#pragma once
#include <vector>
#include <algorithm>
#include <cstdint>

class Sequencer {
public:
    explicit Sequencer(int steps = 16);

    // --- Step on/off ---
    void toggleStep(int step);
    void setStepOn(int step, bool on);

    bool getStepOn(int step) const;

    // --- Velocity (1..127). If step is off, velocity may still be stored but is not played. ---
    void setVelocity(int step, int velocity);   // clamps 1..127
    int  getVelocity(int step) const;           // 0..127
    void changeVelocity(int step, int delta);   // delta can be +/-; clamps

    // --- Clear / queries ---
    void clearSteps();
    bool hasSteps() const;

    // --- Track state ---
    void toggleMute();
    void toggleSolo();
    bool isMuted() const;
    bool isSoloed() const;

    int getNumSteps() const { return numSteps; }

private:
    int numSteps = 0;

    // 0 = off, 1..127 = velocity
    std::vector<uint8_t> stepVel;

    bool muted  = false;
    bool soloed = false;

    static int clampVel(int v) {
        if (v < 1) return 1;
        if (v > 127) return 127;
        return v;
    }
};
