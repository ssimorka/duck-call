# fx-host: test a kennel FX on your computer

Run a hundekraft (kennel) effect plugin on your computer and listen to the result, without an OP-1 or the emulator. It plays 8 seconds of a chord pad over a kick at 120 BPM through your effect and writes `dry.wav` and `wet.wav`.

The hundekraft SDK's `op1_plugin.h` includes `harness/op1_host.h` when `OP1_HOST` is defined, but the 3.2 download doesn't ship that file. This folder has one.

## Use it

From this folder, with `K` pointing at the SDK's `ext/kennel/synth_sdk`:

```sh
cc -O2 -Wall -fsanitize=address,undefined -I. -I$K \
   -DPLUGIN_C="\"$K/plugins/myfx/myfx.c\"" -DFX=FX_myfx \
   fx_host.c -lm -o fx_host
./fx_host 8000 16000 4000 32767
```

- The four numbers are the knobs (blue, green, white, red), from 0 to 32767.
- Three more numbers set the page-2 words for blue, green and white, if your effect uses encoder presses.
- Add `-DSTEREO` for a stereo effect (`"stereo": true` in plugin.json).
- `op1_sc_source()` returns the kick for every sidechain source, and the beat clock runs at 120 BPM.
- The sanitizers catch out-of-bounds reads and integer overflow, which would crash or click on the OP-1.

Duck Call, for example:

```sh
cc -O2 -fsanitize=address,undefined -DSTEREO -I. -I$K \
   -DPLUGIN_C="\"$K/plugins/duckcall/duckcall.c\"" -DFX=FX_duckcall fx_host.c -lm -o fx_host
./fx_host 12000 21533 21845 5120 5500 16384 32767
```

Host knob words go straight through (`op1_fx_param_q15` returns them as 0..32767). Screens aren't drawn here; check your screen art with the SDK's `harness/svg_check.py`.
