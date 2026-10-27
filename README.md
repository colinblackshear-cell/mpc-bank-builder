# MPC Bank Builder

A Mac app that builds kits for the Akai MPC2000XL. Drop samples on a 4x4 pad grid laid out like the MPC,
hear them from the app or your MIDI keyboard, then export. Each kit becomes a folder of native `.SND` sounds and
a `.PGM` program with every pad already assigned. Copy it to the MPC's card and load it.

## Get the latest build

1. Open the [Actions tab](../../actions/workflows/build.yml) and click the newest green run.
2. Under **Artifacts**, download **MPC-Bank-Builder-mac** and unzip it twice to get `MPC Bank Builder.app`.
   Move it to `Applications`.
3. The build isn't notarized by Apple, so macOS blocks it at first. Open Terminal and run:

   ```sh
   xattr -dr com.apple.quarantine "/Applications/MPC Bank Builder.app"
   ```

## Make a kit

1. Drag samples (WAV, AIFF, MP3, FLAC or MPC `.SND`) onto a pad. Drop several files, or a whole folder, and they
   fill the pads in order from the one you dropped on. Pads A01 to A16 are bank A; the A, B, C, D buttons switch
   banks, so a kit holds up to 64 pads.
2. Click a pad to hear it. A MIDI keyboard plays the pads on the same notes the MPC uses (A01 is note 37), and
   **Audio/MIDI...** picks your output and keyboard.
3. Set each pad's name, volume, pan, tune and how it plays in the panel on the right. **One shot** plays to the end,
   **Mono** cuts off the last hit (good for hi-hats), **Note off** stops when you let go.
4. Name the kit at the top. **New kit** starts another one; everything in the list exports together.

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
- `Source/app/` is the JUCE app: pad grid, pad settings, audio and MIDI playback, sample loading.
- `tests/` has the format tests and a headless test of loading, rendering and exporting.

## Coming next

- Chopping: load a long recording and slice it onto pads by hand, at the hits (transient detection), or on a
  beat grid.
- Sampling straight into the app from your audio interface.
