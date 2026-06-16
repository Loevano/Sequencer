# Sequencer

A macOS CoreMIDI step sequencer for controlling MIDI notes from a Novation Launch Control XL and routing them into Ableton Live or another MIDI host.

The project builds a command-line macOS tool named `Sequencer`. It creates virtual MIDI ports, listens to a Launch Control XL for button and pot input, follows external MIDI clock by default, and outputs MIDI notes for a 16 x 16 step-sequencer layout.

## What it does

- Maintains 16 user banks.
- Each bank has 16 tracks.
- Each track has 16 steps.
- Each step stores an on/off state plus MIDI velocity.
- Each track supports mute and solo.
- Sends one MIDI note per track, starting at MIDI note 36 / C1.
- Uses MIDI channels 1-16 for the 16 user banks.
- Drives Launch Control XL LEDs to show playhead, active steps, velocity levels, selected track, menu states, and playing tracks.
- Syncs to external MIDI clock by default.

## Hardware and MIDI assumptions

The code currently looks for this controller endpoint name:

- `Launch Control XL` for controller input.

It also creates these virtual MIDI ports:

- `Sequencer Out`: MIDI output from this app to Ableton or another host.
- `Sequencer In`: MIDI input from Ableton or another host.

MIDI clock can arrive through `Sequencer In`. The app also attempts to connect to an existing source named `MIDI Port` for clock input, but that source is optional if your host is sending clock to `Sequencer In`.

If the Launch Control XL source is not found, the app falls back to connecting all MIDI sources, but the control mapping is still written for the Launch Control XL.

## Control mapping

### Step mode

This is the default mode when the bank button is not held.

| Control | MIDI CC | Behavior |
| --- | ---: | --- |
| Pads | 33-48 | Toggle steps on the selected track |
| Pots | 1-16 | Scale velocity for tracks 1-16 in the active bank; LED flashes when that track's note plays |
| Send Select 1 | 49 | Raise the default velocity level for new steps |
| Send Select 2 | 50 | Lower the default velocity level for new steps |
| Track Select Prev | 51 | Select the previous device/track slot, stopping at 1 |
| Track Select Next | 52 | Select the next device/track slot, stopping at 16 |
| Clear / Record Arm | 56 | Hold as a channel/folder menu |
| Mute while Clear / Record Arm is held | 54 | Enter or exit channel mute action |
| Solo while Clear / Record Arm is held | 55 | Enter or exit channel solo action |
| Pads while Clear / Record Arm is held | 33-48 | Select, mute, or solo active user banks / MIDI channels depending on current action |

Velocity uses three fixed levels, shown by the existing low/mid/full step LED feedback:

- `32`
- `80`
- `120`

### Bank mode

Hold the bank button to manage tracks.

| Control | MIDI CC | Behavior |
| --- | ---: | --- |
| Bank / Device | 53 | Momentary bank/track-management and device mode |
| Mute | 54 | Enter or exit mute action |
| Solo | 55 | Enter or exit solo action |
| Clear | 56 | Enter or exit clear action |
| Pads | 33-48 | Select, mute, solo, or clear tracks depending on current action |
| Solo + Clear while Bank is held | 55 + 56 | Clear all solos in the active bank |

When no mute/solo/clear action is selected, pads in bank mode select the active track for the current user bank.
When no mute/solo action is selected, pads in the Clear / Record Arm channel menu select the active user bank / MIDI channel.

## Playback and sync

External MIDI clock is enabled by default in `MidiInterface`.

The sequencer responds to:

- MIDI Start (`0xFA`)
- MIDI Continue (`0xFB`)
- MIDI Stop (`0xFC`)
- MIDI Clock (`0xF8`)

It advances one step every 6 MIDI clock pulses, which corresponds to 16th-note timing at the standard 24 pulses per quarter note.

There is also an internal clock path in `main.cpp` that advances every 150 ms, but it only runs when MIDI clock sync is disabled in code.

## MIDI output

On each step:

1. Notes from the previous step are turned off.
2. Active notes on the current step are sent.
3. Track mute/solo state is applied.
4. Step velocity is scaled by the matching track pot value.

Track-to-note mapping starts at note 36:

| Track | MIDI note |
| ---: | ---: |
| 1 | 36 |
| 2 | 37 |
| ... | ... |
| 16 | 51 |

The active user bank determines the MIDI channel.

## Build

This is an Xcode macOS command-line tool project using:

- C++20
- CoreMIDI.framework
- CoreFoundation.framework
- macOS deployment target 15.2

Open `Sequencer.xcodeproj` in Xcode and build/run the `Sequencer` target.

For command-line builds, make sure `xcode-select` points at a full Xcode installation rather than only the Command Line Tools package:

```sh
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
xcodebuild -project Sequencer.xcodeproj -scheme Sequencer build
```

## Run

1. Connect the Launch Control XL.
2. Select `Sequencer Out` as an input in the MIDI host.
3. Send MIDI clock from the host to `Sequencer In`.
4. Run the `Sequencer` executable.
5. Start transport in the MIDI host.

The app prints:

```text
Running. Ctrl+C to quit.
```

Stop it with `Ctrl+C`.

## Current limitations

- Endpoint matching is name-based and currently hard-coded.
- There is no persistence; patterns are lost when the process exits.
- There is no command-line configuration.
- The app is written for a fixed 16-bank, 16-track, 16-step layout.
- `track.h` contains an older/simple track model that is not used by the current `main.cpp` path.
