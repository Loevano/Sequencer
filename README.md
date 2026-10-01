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

Selecting User templates 1-8 on the Launch Control XL selects independent banks
on MIDI channels 1-8. Factory templates 1-8 select banks on channels 9-16.
Returning to a template restores its patterns and selected track; changing
templates does not erase patterns or stop other banks playing.
Each template must use the CC mapping below, with momentary buttons sending
127 on press and 0 on release. Factory templates have fixed mappings and may
not match these controls. Select a template after starting the app so it receives
the controller's template-change notification; the app initially selects bank 1.

## Control mapping

### Step mode

This is the default mode when the bank button is not held.

| Control | MIDI CC | Behavior |
| --- | ---: | --- |
| Pads | 33-48 | Toggle steps on the selected track |
| Pots | 1-16 | Scale velocity for note tracks 1-16 across all MIDI channels; LED flashes only when that track's note plays on the selected channel |
| Third-row pots | 17-24 | Unused |
| Faders | 25-32 | Scale all notes on MIDI channels 1-8 respectively, regardless of selected template |
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
| Track Select Prev while Bank is held | 51 | Decrease swing amount toward early offbeats; Track Select LEDs show current swing |
| Track Select Next while Bank is held | 52 | Increase swing amount toward late offbeats; Track Select LEDs show current swing |
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

Swing is pulse-based and shifts odd-numbered 16th steps while keeping even steps locked to the incoming MIDI clock grid. Hold Bank and press Track Select Prev/Next to move through these settings:

| Swing amount | 16th-pair timing | Track Select LEDs while Bank is held |
| ---: | --- | --- |
| -3 | 3 + 9 pulses | Prev fast blink |
| -2 | 4 + 8 pulses | Prev slow blink |
| -1 | 5 + 7 pulses | Prev solid |
| 0 | 6 + 6 pulses | Both off |
| 1 | 7 + 5 pulses | Next solid |
| 2 | 8 + 4 pulses | Next slow blink |
| 3 | 9 + 3 pulses | Next fast blink |

There is also an internal clock path in `main.cpp` that advances every 150 ms, but it only runs when MIDI clock sync is disabled in code.

## MIDI output

On each step:

1. Notes from the previous step are turned off.
2. Active notes on the current step are sent.
3. Track mute/solo state is applied.
4. Step velocity is scaled by the matching track pot value. MIDI channels 1-8 are also scaled by their matching fader.

Each knob scale is shared by the corresponding note track across all banks:
knob 1 scales note track 1 on every MIDI channel, knob 2 scales note track 2,
and so on through knob 16. Rotary LEDs reflect notes played only on the
currently selected MIDI channel. Fader 1 controls
all tracks in the MIDI channel 1 bank, fader 2 controls all tracks in channel 2,
and so on through fader 8 / channel 8. Faders keep this assignment when you
change templates. Channels 9-16 use only their track knob scales.
All scales start at full scale and range from zero (silent) to 127 (full).
For example, a half-scale knob and half-scale channel fader produce roughly
one quarter of the step velocity. These controls preserve the stored step
velocities and their accents.

Knobs and faders snap to the received value as soon as you move them, including
the first movement after startup or a template change. Knobs stay assigned to
the same note tracks across all channels, and faders stay assigned to the same
channels. Untouched controls start at full scale; the controller has no
documented request for their physical positions at startup.

Track-to-note mapping starts at note 36:

| Track | MIDI note |
| ---: | ---: |
| 1 | 36 |
| 2 | 37 |
| ... | ... |
| 16 | 51 |

Each user bank outputs on its corresponding MIDI channel. All banks play
together; the selected bank determines which patterns and controls you edit.

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

Stop it with `Ctrl+C`. On exit, the app turns off notes and clears all button
and rotary LEDs. Stopping the MIDI host's transport keeps the button LEDs
available for editing.

For verbose CoreMIDI startup diagnostics and device listing, run:

```sh
./Sequencer/Sequencer --debug
```

## Current limitations

- Endpoint matching is name-based and currently hard-coded.
- There is no persistence; patterns are lost when the process exits.
- There is no command-line configuration.
- The app is written for a fixed 16-bank, 16-track, 16-step layout.
- `track.h` contains an older/simple track model that is not used by the current `main.cpp` path.
