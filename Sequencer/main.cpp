#include "sequencer.h"
#include "import_midi.hpp"
#include <thread>
#include <chrono>
#include <vector>
#include <iostream>

int main() {
    const int NUM_SEQUENCES = 16;
    const int STEPS_PER_SEQUENCE = 16;

    std::vector<Sequencer> sequences;
    for (int i = 0; i < NUM_SEQUENCES; ++i)
        sequences.emplace_back(STEPS_PER_SEQUENCE);

    int currentSequence = 0;
    bool bankMode = false;

    MidiInterface midi(&sequences, &currentSequence, &bankMode);
    if (!midi.initialize()) return 1;

    std::cout << "Sequencer running with LED refresh thread.\n";

    // --- LED refresh thread ---
    std::thread ledThread([&]() {
        while (true) {
            midi.updateSequencerLeds(sequences[currentSequence], bankMode);
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 50 Hz refresh
        }
    });
    ledThread.detach();

    // --- Step advancement loop ---
    while (true) {
        if (!bankMode) {
            sequences[currentSequence].stepForward();
        }

        // Sleep for 500 ms (adjust for tempo)
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return 0;
}
