#include "sequencer.h"
#include "import_midi.hpp"

#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <iostream>

int main() {
    constexpr int NUM_TRACKS = 16;
    constexpr int STEPS = 16;

    std::vector<Sequencer> tracks;
    tracks.reserve(NUM_TRACKS);
    for (int i = 0; i < NUM_TRACKS; ++i) tracks.emplace_back(STEPS);

    int selectedTrack = 0;
    bool bankMode = false;
    int globalStep = 0;

    MidiInterface midi(&tracks, &selectedTrack, &bankMode, &globalStep);
    if (!midi.initialize()) return 1;

    std::atomic<bool> running{true};

    std::thread clockThread([&](){
        while (running) {
            if (!midi.isUsingMidiClock()) { // only run internal clock when NOT syncing
                if (!bankMode) {
                    globalStep = (globalStep + 1) % STEPS;
                    midi.tickStep(globalStep);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
            } else {
                // synced mode: do nothing here; MIDI clock drives everything
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });


    // LED thread (fast UI)
    std::thread ledThread([&](){
        while (running) {
            if (bankMode) midi.updateBankLeds();
            else          midi.updateStepLeds(33);

            midi.updateMenuLeds();

            std::this_thread::sleep_for(std::chrono::milliseconds(20)); // ~50Hz
        }
    });

    std::cout << "Running. Ctrl+C to quit.\n";
    clockThread.join();
    ledThread.join();
    return 0;
}
