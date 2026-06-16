# Agent Instructions

## Project context

This is a small macOS C++20 CoreMIDI command-line sequencer for a Novation Launch Control XL.

Start with these files:

- `README.md`: user-facing overview, control mapping, build/run notes.
- `Sequencer/main.cpp`: app setup, banks/tracks allocation, clock and LED threads.
- `Sequencer/import_midi.hpp`: MIDI interface state, controller mapping constants, sync state.
- `Sequencer/import_midi.cpp`: CoreMIDI setup, MIDI callback, note output, LED updates.
- `Sequencer/sequencer.h` and `Sequencer/sequencer.cpp`: per-track step/velocity/mute/solo model.
- `Sequencer/track.h`: older/simple track model; currently not used by `main.cpp`.

The app creates virtual ports named `Sequencer In` and `Sequencer Out`. It also has hard-coded assumptions for Launch Control XL controls and, optionally, a MIDI clock source name.

## Git workflow

- Check `git status --short` before editing.
- Do not revert or clean up changes you did not make.
- Keep commits focused when committing is requested.
- Treat generated build products and local Xcode user files as incidental unless the user explicitly asks to manage them.
- `README.md` and `AGENTS.md` are project docs and should stay in the repo.
- Prefer inspecting diffs before finalizing: `git diff -- <files>`.

## Build and run

The Xcode target is a macOS command-line tool named `Sequencer`.

The checked-in project expects full Xcode for `xcodebuild`. If the active developer directory is only Command Line Tools, `xcodebuild` may fail until Xcode is selected.

A direct local compile can be useful for quick validation:

```sh
clang++ -std=c++20 Sequencer/main.cpp Sequencer/sequencer.cpp Sequencer/import_midi.cpp -framework CoreMIDI -framework CoreFoundation -o Sequencer/Sequencer
```

Run the local binary from the repo root with:

```sh
./Sequencer/Sequencer
```

Do not run the app as a verification step unless the user wants live MIDI testing; it enters a hardware-dependent loop.

## Coding style

Prefer simple, compact code that matches the current codebase.

- Keep control flow readable and direct.
- Avoid piling on guard checks unless they protect a real boundary or failure mode.
- Do not add abstractions unless they remove actual duplication or clarify an existing concept.
- Keep the fixed 16-bank, 16-track, 16-step shape explicit unless the user asks to generalize it.
- Prefer named constants for MIDI CCs, notes, and protocol values.
- Keep Launch Control XL-specific behavior in `MidiInterface` unless there is a clear reason to separate it.

This project is a practical hardware tool, not a framework. Optimize for code that is easy to scan while debugging MIDI behavior.

## Comments

Use comments sparingly and only where they help with MIDI or hardware context.

Good comments explain:

- why a specific CC, note, or realtime MIDI byte is used;
- a Launch Control XL LED encoding quirk;
- a DAW routing or sync assumption;
- state-machine behavior that is not obvious from the code.

Avoid comments that restate the next line of code. If a comment is needed because a block is hard to follow, first consider simplifying the block.

## Testing guidance

There is no automated test suite currently.

For code-only changes, validate by compiling when practical. For MIDI behavior changes, explain what was checked in code and what still needs live testing with the Launch Control XL and Ableton or another MIDI host.
