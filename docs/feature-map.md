# OilSlick: research and feature map

Written 4 November 2026. OilSlick recreates the behaviour of a polyphonic pitch-shifting modulator pedal (the EarthQuaker Devices Rainbow Machine) in the Flip Fella family look. All DSP is original code written from public descriptions of the pedal. The plugin UI names no brands.

## Versions

| Version | Year | What changed |
| --- | --- | --- |
| Original (V1) | 2014 | Six knobs (Tracking, Tone, Pitch, Primary, Secondary, Magic), two latching footswitches (Activate and Magic), an expression jack that takes over Pitch |
| V2 | January 2018 | Same six knobs and jack. About twice the output level, longer Tracking delay, deeper chorus, lower noise floor, recalibrated Magic taper, silent relay "flexi" footswitches (tap to latch, hold for momentary) on both Activate and Magic |
| "R3" | 2020 | A manual revision for the V2 (document code EQDRMV2-EU-R3), not new hardware |

**Recommendation: recreate V2.** Hold-for-momentary Magic, which the runaway idea is built on, only exists on V2. V2's longer Tracking and recalibrated Magic also give the widest range of sounds.

**Decided (Kevin, 4 November 2026):** V2, plus a First edition switch: quieter (half the wet level), hissier, Tracking to 500 ms instead of 750 ms, a Magic knob that reaches oscillation sooner and a grittier loop. Nobody publishes measured differences, so it is tuned by ear. Pitch stays within the pedal's range everywhere. Jump on C and Freeze on D as proposed. The expression pedal gets an on-screen Pitch or Magic toggle.

## Controls (from the manual)

| Control | What the pedal does | How OilSlick does it |
| --- | --- | --- |
| Tracking | Lag between dry and pitch-shifted signal. Full clockwise stacks the harmony right on the note (tight). Lower settings make it arpeggio-like, then delay-like, then ambient | A delay before the shifters: about 2 ms at full clockwise to 750 ms at full anticlockwise (500 ms on the first edition), on an exponential taper. No source gives the real figure, so this is the closest estimate. Moving it while signal is in the line warps the pitch like a tape delay |
| Tone | Treble roll-off. Clockwise bright, anticlockwise dark ("vintage") | A gentle 2-pole low-pass on the wet path from about 1.2 kHz to 16 kHz, also inside the Magic loop so dark settings give darker, Echoplex-like repeats |
| Pitch | Harmony interval, continuously variable from a fourth below to a third above. Noon is unison, which works as a chorus. Below noon, lingering repeats descend. Above noon they climb | Continuous −5 to +4 semitones, unison at noon. Near unison, a slow random detune makes it chorus. Each pass through the Magic loop is shifted again, so trails climb or fall as on the pedal. Optional Snap to intervals |
| Primary | Level of the main shifted voice. Unity around 2 o'clock | Wet level of voice 1, 0 to +6 dB, unity at 2 o'clock |
| Secondary | A second voice an octave from the Primary. Noon is off, clockwise adds the octave above, anticlockwise the octave below | A bipolar knob, noon off. Voice 2 sits at Pitch ±12 semitones with its own crossfade timing, so the two voices wobble against each other |
| Magic (knob) | Regeneration for both shifters. They feed into each other and themselves: extra repeats, then ambience and chorus, then pitch aliasing ("pixie trails"), controllable self-oscillation, and finally distortion. Works only when the Magic switch is on and Primary or Secondary is up | A 2×2 feedback matrix (each voice feeds itself and the other) with a soft clipper in the loop. Loop gain reaches 1 at about 85% of the knob and goes beyond it after that, so the clipper holds the oscillation and turns it into distortion. Aliasing comes from a deliberately unfiltered sample-and-hold stage in the loop. Silent when both voice levels are at zero, as on the pedal |
| Activate footswitch | Bypass. V2: tap latches, hold is momentary | Footswitch A, same behaviour. Trails optional |
| Magic footswitch | Magic on or off. V2: tap latches, hold is momentary | Footswitch B. Tap latches as on the pedal. Hold is the runaway (see below) |
| Expression jack | Controls Pitch and defeats the Pitch knob | The expression pedal drives Pitch by default. Touching the Pitch knob on screen takes control back |

## The character to keep

Reviews and the maker's own text describe a pedal that isn't trying to be clean: "not for purists", glitchy and warbly, odd-order harmonics when Magic is up. The plugin keeps that on purpose:

- **Splice glitches.** A two-head delay-line pitch shifter with short crossfades that are deliberately not phase-aligned, so you hear the splice warble rather than a smooth modern shifter.
- **Two voices that disagree.** Voice 1 and voice 2 use different splice lengths, so they never line up.
- **Drift.** Slow random wander on the splice timing and the near-unison detune, the "wobbly" part.
- **Pixie trails.** In the Magic loop the signal is resampled with no anti-alias filter. Above about a third of the knob this folds high notes into metallic, glassy trails.
- **Climbing and diving.** Each pass through the loop is pitch-shifted again, so trails stack upward or downward depending on which side of noon Pitch sits.
- **Oscillation that you can play.** Past about 85% the loop runs away on its own, and Pitch and Tracking bend the scream. Near the top it goes into distortion, which the pedal also does.
- **Tracking moves warp.** Sweeping Tracking during oscillation bends pitch, a classic trick in the demos.

## Footswitches (proposed defaults, CC 20 to 23 on channel 16, any channel when read straight from USB)

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass on or off (Activate) | Momentary: effect on while held, as on V2 |
| B | Magic on or off, at the knob's setting (as on the pedal) | **Runaway**: Magic engages and climbs from the knob's setting into self-oscillation over about 2 seconds. Let go and it drops back to the knob's setting while the scream rings out |
| C | **Jump**: toggle Pitch between its setting and the Jump interval (a third up by default) with a short glide | **Dive**: Pitch glides to the bottom of its range while held and springs back on release |
| D | **Freeze** on or off: the current trail loops at unity gain without growing, and you play over it | Freeze while held |
| A+B | **Tame**: kill the feedback and trails instantly, for when it gets away from you | |
| C+D tap | Hand the footswitches to the next OilSlick | |
| C+D held 1 second | Keeper | |

Why: A and B are the two switches the pedal has, so muscle memory carries over. B's tap matches the pedal, and the hold adds the runaway everyone fakes by reaching down to crank the knob. C is the dive-bomb people use the expression pedal for, plus a quick harmony jump, so you get pitch moves without the expression pedal. D, Freeze, isn't on the pedal, but it turns a good Magic swell into a pad you can play over. Tame is the safety net any self-oscillating pedal needs live. Keeper stays on hold C+D, and C+D tap keeps its family meaning. The combo window stops C or D's single-switch jobs from firing first, so nothing clashes.

## Expression (CC 11 on channel 16)

Default target: **Pitch**, the job the pedal's own jack does, and the most playable. The heel to toe sweep is shaped musically:

- Heel is a fourth down, toe a third up, as on the knob's ends.
- A small flat zone in the middle holds unison, so a resting foot doesn't drift out of tune.
- Each half is gently drawn towards whole semitone intervals, so a slow sweep lands on harmonies while a fast sweep still glides smoothly for dive-bombs.
- After Calibrate heel and toe, the centre of the zone is the centre of your pedal's travel.

Alternative target: **Magic**, on a curve that spends most of the travel near the edge of oscillation (heel is a few repeats, half travel is pixie trails, the last fifth is runaway), so the interesting part isn't squeezed into the final centimetre.

## Family conventions

- `Source/PedalControl.*` copied from Prism Patch, with Learn, calibration, curve, direct USB reading, combo holds and shared `Flip Fella Pedals.xml`.
- `Source/Keeper.*` copied from Prism Patch. Hold C+D for 1 second keeps the last 8 seconds.
- An original pedal MIDI map isn't needed: the pedal has no MIDI.
- Pages: Main, Deep, Pedal. Every footswitch job has an on-screen button.
- Engine in plain C++17 with no JUCE, plus an offline render test.
- Builds: Windows VST3, macOS, and Linux ARM64 through GitHub Actions, published to the `builds` branch.

## Sources

- Rainbow Machine manuals (original, V2, EU R3) and the maker's product page, as quoted in retailer listings and manual mirrors
- V2 launch coverage, January 2018 (guitarefx.blogspot.com)
- Premier Guitar and Harmony Central reviews, Pedal of the Day (September 2014), Bass Gear Reviews
- "Aaron's Bass Hole: Hitchhiker's Guide to the Rainbow Machine", the maker's blog, 2016
