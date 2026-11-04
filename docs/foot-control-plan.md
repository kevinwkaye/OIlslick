# OilSlick foot control plan

Written 4 November 2026. Built in the first version, following the house rules and Prism Patch's `PedalControl`.

## Footswitch defaults

Momentary CC 20, 21, 22, 23 on channel 16 (any channel when read straight from the footswitch's USB cable). Learn remaps any of them.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass (Activate), acting on the press | Momentary: flips while held, as the V2's flexi switch |
| B | Magic on or off, as on the pedal | Runaway: Magic climbs into self-oscillation over about 2 seconds, drops back on release |
| C | Jump a third up and back | Dive to a fourth down, springs back on release |
| D | Freeze on or off | Freeze while held |
| A+B | Tame: kill the feedback and trails | |
| C+D | Hand the footswitches to the next OilSlick | Held 1 second: Keeper |

Why: A and B are the pedal's own two switches, so muscle memory carries over, and B's hold adds the runaway people fake by cranking the knob. C gives dive-bombs without an expression pedal. D isn't on the pedal, but turns a good Magic swell into a pad to play over. Tame is the safety net a self-oscillating effect needs live.

A and D act on the press, so they're instant. If the press turns out to be half of a pair (A+B or C+D within 0.12 seconds), the press is undone, so pairs never leave a stray bypass or freeze behind.

## Expression default

CC 11 on channel 16. Moves Pitch, as the pedal's own jack does, with an on-screen toggle to Magic and a menu for every other control. Pitch is shaped: unison rest zone in the middle, gentle pull to semitones. Magic is shaped so most of the travel is near the edge of oscillation. Heel and toe calibration, curve and the shared settings come from `PedalControl`. Min, max and smoothing are per preset.

## Original pedal MIDI map

None: the pedal has no MIDI, so there's nothing to clash with the footswitch and expression defaults.

## Tests

`Tests/FootTest.cpp` covers tap, hold, A+B and C+D (both orders), the 1 second Keeper hold, both expression curves, the remote parameters, and a clash test: sustain (CC 64), mod wheel (CC 1), and CC 20 to 23 and 11 on channels 1, 2 and 15 change nothing.
