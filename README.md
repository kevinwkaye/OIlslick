# OilSlick

A VST3 for Ableton Live, recreating the EarthQuaker Devices Rainbow Machine polyphonic pitch modulator (V2, with the first edition as a switch) in the Flip Fella look. All DSP here is original code written from public descriptions of the pedal's behaviour.

Two pitch-shifted voices trail your playing, and Magic feeds them back into each other until they climb, dive, sparkle and finally scream on their own. It is meant to be wobbly, glitchy and unstable.

## Install

1. Open the **builds** branch of this repo and download `OilSlick-Windows.zip`.
2. Unzip it and copy the `OilSlick.vst3` folder to `C:\Program Files\Common Files\VST3`.
3. In Ableton, rescan plugins (Preferences, Plug-Ins, Rescan). OilSlick appears under Audio Effects.

The same branch has `OilSlick-Linux-ARM64.zip`, built for the future Raspberry Pi rig.

## The panel

| Control | What it does |
| --- | --- |
| **Pitch** | The harmony, from a fourth down to a third up. Noon is unison, a wobbly chorus. Above noon the Magic trails climb as they repeat, below noon they fall |
| **Primary** | Level of the main shifted voice. Unity at about 2 o'clock |
| **Secondary** | A second voice an octave from the Primary: clockwise an octave above, anticlockwise an octave below. Noon is off |
| **Tracking** | The lag between your note and the shifted voices: tight at full clockwise, out to 750 ms (500 ms on the first edition). Turning it while Magic is screaming bends the pitch like tape |
| **Tone** | Treble roll-off, dark to bright |
| **Magic** | Feedback between the two voices. Low: a few extra repeats. Middle: chorus, then pixie trails (glassy, aliased sparkle). Past the white mark the loop feeds itself and oscillates, and at the very top it distorts. Only works with the Magic switch on and the Primary or Secondary up, as on the pedal |
| **V2 / First edition** | V2 is louder and cleaner with longer Tracking. The first edition is quieter and hissier, with a Magic knob that takes off sooner, tuned by ear |
| **Trails** | Let the repeats ring out when bypassed (they always die away) |
| **Tame** | Cuts the feedback and trails instantly |
| **Hold: run away** | Holds the runaway on screen, like footswitch B |

**Pebble** floats up as the pitch rises and sinks as it falls, the oil-slick ring shifts colour with the pitch, pixie dots spin off it as Magic rises, and in a runaway it shakes, stares and screams. It holds its breath for Freeze, dozes when bypassed, taps its foot on a footswitch press, leans with the expression pedal and nods when you keep a moment.

## Footswitches and expression (optional)

Everything works from the screen, so you don't need any pedals. The four squares along the bottom act exactly like footswitches, taps, holds and pairs included. If you have a MIDI footswitch:

1. Set its first four switches to send **momentary CC 20, 21, 22 and 23 on MIDI channel 16** (127 when pressed, 0 when released). Or use **Learn** on the Pedal page to use whatever yours sends.
2. Set an expression pedal input to **CC 11 on channel 16**, or click the expression bar and choose Learn.
3. Click the expression bar, choose **Calibrate heel and toe**, then rock the pedal from heel to toe once.

These settings are shared by every Flip Fella plugin on your computer, so you set them up once.

| Footswitch | Tap | Hold |
| --- | --- | --- |
| A | Bypass on or off (acts on the press) | Momentary: flips while held |
| B | Magic on or off, as on the pedal | **Run away**: Magic climbs from the knob's setting into self-oscillation over about 2 seconds, then drops back when you let go |
| C | **Jump** a third up, tap again to come back | **Dive**: glides to a fourth down, springs back on release |
| D | **Freeze** on or off: the last moment of the trail holds as a pad you play over | Freeze while held |
| A+B | **Tame**: kill the feedback now | |
| C+D | Hand the footswitches to the next OilSlick | Held 1 second: **Keeper** |

**Expression** moves Pitch by default, as the pedal's own jack does. The PITCH and MAGIC buttons on the expression bar switch between the two, and the menu reaches Tracking, Tone, Primary and Secondary. Heel to toe is shaped for playing rather than a straight line:

- On **Pitch**, heel is a fourth down and toe a third up. A small resting zone in the middle holds unison, and the sweep is gently drawn to whole semitones, so slow moves land on harmonies and fast moves still glide.
- On **Magic**, most of the travel sits where it gets interesting: heel is a few repeats, half way is pixie trails, the last fifth is the edge of oscillation and beyond. Moving the pedal switches Magic on.

The Pedal page sets what the heel and toe reach, smoothing, and the curve.

### Getting footswitch MIDI to the plugin in Ableton

OilSlick reads your footswitch straight from its USB cable, so every copy you load works with no MIDI track. The dot next to **FOOTSWITCHES** turns green when it's connected, and Pebble says FOOTSWITCHES READY.

1. In Ableton's Preferences, Link Tempo MIDI, turn **Track**, **Sync** and **Remote** off for your footswitch's input. Windows lets only one program use a MIDI input at a time, so Ableton has to let go of it.
2. Load OilSlick. That's it.

If the dot stays grey, click **FOOTSWITCHES**. The menu says what's wrong, and you can pick your footswitch from the list if it wasn't found automatically.

With several copies loaded, the footswitches drive the one you opened or clicked last. Tap C+D to hand them to the next copy.

Prefer routing through Ableton? Choose **Only from a MIDI track in Ableton** in that menu, then turn **Track** back on for the footswitch, make a MIDI track with **MIDI From** set to the footswitch and **MIDI To** set to OilSlick, and arm it.

The pedal itself has no MIDI, so there is no original pedal MIDI map to clash with the footswitches. Anything else on the cable, the sustain pedal on CC 64 included, is ignored.

### Kevin's setup (M-Vave Chocolate Plus and M-Audio EX-P)

In CubeSuite: Custom mode, footswitches A to D as momentary CC 20 to 23, the pedal jack as Expression Pedal on CC 11. It always sends on channel 1 whatever CubeSuite says; read directly from its cable, OilSlick accepts any channel. Put the Chocolate Plus switch on **U** and connect with the USB cable, not Bluetooth. The EX-P needs a 3.5 mm to 6.35 mm TRS adapter and reads backwards on the Chocolate Plus, which Calibrate fixes.

## Keeper

Press **KEEP** (or hold C+D for a second) to log the moment: every setting, the expression pedal, the tempo, and the last 8 seconds of input and output as WAV files, into `Documents\Flip Fella Keepers`. The small arrow next to KEEP adds a note, exports a zip for Claude, opens the folder, sets Live or Studio, and learns a dedicated Keeper switch.

## Remote control

Every control, every footswitch job and Keeper are in one parameter list that a host or a remote controller can read and set. `footA` to `footD` act as the footswitches (1 to press, 0 to release, so taps, holds and pairs all work), `keeper` and `tame` fire once and reset, and `freeze`, `jump`, `dive` and `runaway` can be set directly.

## CPU

Per instance, one core, Magic and both voices on (the heaviest case):

| Machine | 48 kHz, 64 samples | 48 kHz, 128 samples |
| --- | --- | --- |
| Windows x64, GitHub runner (V2) | 0.82% | 0.82% |
| Windows x64, GitHub runner (first edition) | 0.86% | 0.85% |
| Linux ARM64, GitHub runner (V2) | 1.19% | 1.18% |
| Linux ARM64, GitHub runner (first edition) | 1.20% | 1.19% |

Measured for v0.1 on 4 November 2026. A Raspberry Pi 5 core is slower than these runners, so expect a few times these figures there, still small.

Every build writes fresh figures for Windows x64 and Linux ARM64 to `cpu-windows-x64.txt` and `cpu-linux-arm64.txt` on the builds branch.

## Building

GitHub Actions builds the Windows VST3 and the Linux ARM64 build on every push and publishes them to the `builds` branch. To build locally: `cmake -S . -B build && cmake --build build --config Release`. JUCE downloads automatically on the first configure.

## Tests

- `oilslick_render_test` runs the engine with no host: 22 scenarios at three sample rates (every control, Magic from repeats to burn, the runaway really self-oscillating, Tame, Freeze, bypass with and without trails, Jump, Dive, both versions), checks for NaNs, levels above full scale and dead output, then measures CPU. Pass a folder to write a WAV of each scenario.
- `oilslick_foot_test` drives the real plugin with plain MIDI press and release messages: tap, hold, A+B, C+D, the 1 second Keeper hold, the expression curves, the remote parameters, and a clash test (sustain pedal, mod wheel and the footswitch numbers on other channels change nothing). Run it with `--snapshot <folder>` under a display to render the panel in six states.

## Project layout

| Path | What it holds |
| --- | --- |
| `Source/engine/` | The DSP engine, plain C++17 with no JUCE |
| `Source/Params.h` | Every parameter, its range and default, and the expression curves |
| `Source/PluginProcessor.*` | Host wrapper, footswitch jobs, expression, Keeper, state |
| `Source/PluginEditor.*` | The Main and Pedal pages, and Pebble |
| `Source/PedalControl.*`, `Source/Keeper.*` | Shared with every Flip Fella plugin, copied from Prism Patch |
| `docs/` | The research, feature map and footswitch plan |
