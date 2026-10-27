# MPC Bank Builder

A Mac app that builds kits for the Akai MPC2000XL. Record sounds from your mic or drop in samples, mangle them
with effects, chop them onto a 4x4 pad grid laid out like the MPC, keep them all at one tempo, then export. Each
kit becomes a folder of native `.SND` sounds and a `.PGM` program with every pad already assigned. Copy it to the
MPC's card and load it.

## Get the latest build

1. Open the [Actions tab](../../actions/workflows/build.yml) and click the newest green run.
2. Under **Artifacts**, download **MPC-Bank-Builder-mac** and unzip it twice to get `MPC Bank Builder.app`.
   Move it to `Applications`.
3. The build isn't notarized by Apple, so macOS blocks it at first. Open Terminal and run:

   ```sh
   xattr -dr com.apple.quarantine "/Applications/MPC Bank Builder.app"
   ```

## Sample and edit (Sampler tab)

1. Press **Record** to sample from your mic or audio interface (pick the input in **Audio/MIDI...**). The app only
   turns the input on when you first record, so that's when macOS asks for microphone access. Press again to stop.
   Or click a sound in the **Sounds** list on the left to open it.
2. Drag across the waveform to select. **Keep selection**, **Delete**, **Trim silence**, **Fade in/out**,
   **Normalize** and **Reverse** work on the selection (or the whole sound). Scroll to zoom, shift-scroll to move.
   **Undo**/**Redo** go back 50 steps.
3. Effects: pitch, speed (changes length, keeps pitch), low and high cut with resonance, drive, bit crusher, sample
   rate reducer, delay and reverb. With **Hear effects** on, Play plays the result. **Apply effects** bakes them in.
4. Chop: **Find hits** puts a marker at each hit (the slider sets how easily), **Even slices** cuts it into 2 to 64
   equal parts, and double-clicking the waveform adds a marker. Drag a marker's tab to move it, right-click it to
   remove it.
5. Then **Save to library**, **Send to pad** (the pad selected on the Kits tab), or **Chop to pads** to put every
   slice on its own pad from the selected one. Effects are applied on the way.

The **Sounds** library is a folder of WAV files at `Music/MPC Bank Builder/Sounds`. **Show folder** opens it in
Finder; anything you put there shows up in the list. Right-click a sound to rename it or move it to the Trash.

## Make a kit

1. Drag samples (WAV, AIFF, MP3, FLAC or MPC `.SND`) onto a pad. Drop several files, or a whole folder, and they
   fill the pads in order from the one you dropped on. Pads A01 to A16 are bank A; the A, B, C, D buttons switch
   banks, so a kit holds up to 64 pads.
2. Click a pad to hear it. A MIDI keyboard plays the pads on the same notes the MPC uses (A01 is note 37), and
   **Audio/MIDI...** picks your output and keyboard.
3. Set each pad's name, volume, pan, tune and how it plays in the panel on the right. **One shot** plays to the end,
   **Mono** cuts off the last hit (good for hi-hats), **Note off** stops when you let go.
4. Name the kit at the top. **New kit** starts another one; everything in the list exports together.

Drag sounds from the **Sounds** list onto pads too, or click one to load it onto the selected pad.

### Keep everything in time

Set the kit's **BPM** and turn on **Keep in time**. Every loop is stretched (without changing its pitch) so it lasts
a whole number of beats at that tempo, so loops recorded at different speeds play together. The app guesses each
loop's length (1, 2, 4, 8... beats); change it under **Length** in the pad settings. Short hits like kicks and snares
are never stretched. Set a pad to **Play normal** to keep it exactly as recorded. Turn **Keep in time** off and every
pad plays as recorded again. Export writes the stretched sounds and sets their beat count on the MPC.

The memory readout shows how much of the MPC's sample memory a kit takes. Pick your MPC's RAM size next to it
(2 MB stock, up to 32 MB). **Save as mono** halves a pad's size.

## Put it on the MPC

1. Put the MPC's SD card in your Mac and click **Export to card...**. Choose the card (or any folder).
2. Each kit is written to its own folder, for example `BREAKS/BREAKS.PGM` with its sounds next to it. Exporting
   again replaces that kit's sounds.
3. Eject the card, put it in the MPC, go to **LOAD**, open the kit's folder and load the `.PGM`. The MPC loads its
   sounds with it.

File names on the card are 8 characters, uppercase, so a pad named "Kick Drum" is saved as `KICKDRUM.SND`.

**Open kit...** opens a `.PGM` from the card, with its sounds, so you can change an old kit or one you exported.

## Build it yourself

```sh
cmake -S . -B build -G Xcode
cmake --build build --config Release
ctest --test-dir build -C Release
```

`-DMPC_TESTS_ONLY=ON` builds just the file format tests, without downloading JUCE. Set `MPC_FIXTURES` to a folder
of files saved by an MPC2000XL to also check that each one reads and writes back byte for byte (CI uses the demo
kits from [vMPC2000XL](https://github.com/izzyreal/mpc)).

## Layout

- `Source/mpc/` reads and writes `.SND` and `.PGM` files and writes kit folders. Plain C++, no JUCE.
- `Source/app/` is the JUCE app: the Sampler and Kits pages, waveform editor, sound library, effects, chopping and
  time-stretching (`SoundFx`, using [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch)),
  audio and MIDI playback and recording.
- `tests/` has the format tests and a headless test of loading, rendering and exporting.

## Coming next

- A phone version for recording and building the sound library on the go.
