#include "sequencer.h"
#include "import_midi.hpp"
#include <thread>
#include <chrono>

#define RUN_SEQUENCER 1
#define RUN_MIDI 1

int main() {
#if RUN_SEQUENCER
    Sequencer seq(16);
#endif

#if RUN_MIDI
    MidiInterface midi(&seq); // pass sequencer reference
    if (!midi.initialize()) {
        std::cerr << "Failed to initialize MIDI\n";
        return 1;
    }
#endif

    std::cout << "Running sequencer + MIDI control... (Ctrl+C to exit)\n";

    while (true) {

    #if RUN_SEQUENCER
        seq.stepForward();
        seq.printSequence();

        // Update LEDs to reflect both step state and current step
        midi.updateSequencerLeds(seq);
    #endif

    #if RUN_MIDI
        midi.readMidi();
    #endif

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
