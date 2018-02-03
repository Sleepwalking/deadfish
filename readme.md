DEADFISH
===

A very simple .wav noise reduction tool with some extra features. I wrote this tool because the algorithm implemented by SoX has some flaws in the short-time Fourier transform, and fixing it is likely to take more lines than rewriting one.

LICENSE: BSD

> How important is impedance matching in audio applications?

> The answers depend on whether you're a electrical engineer or audiophoole. If the latter, we can mumble on at length about oxygen free cables, exra-phancy capacitors, and lots of other expensive nonsense you must follow while waving a dead fish over your amplifier during a full moon. -- Olin Lathrop

> https://electronics.stackexchange.com/questions/6846/how-important-is-impedance-matching-in-audio-applications/6854

How to Use
---

### Denoising

First generate a noise profile from a .wav file that contains only noisy samples.

`deadfish noise.wav -a noise-profile`

Then apply the noise profile on to the .wav file you want to denoise.

`deadfish input.wav output.wav -d noise-profile`

The degree/rate of noise reduction can be controlled using option `-r`. The default is `-r 1.0`.

### Look-ahead Compression

Apply look-ahead compression to input.wav with threshold at 0.1 amplitude level and a compression ratio of 0.2.

`deadfish input.wav output.wav -c 0.1,0.2`

Apply look-ahead compression to input.wav with threshold at -25 LKFS and a compression ratio of 0.2.

`deadfish input.wav output.wav -c -25,0.2,k`

The time-resolution can be controlled by the `-i` option; the unit is in seconds.

### Normalization

Set the maximum amplitude to 0.5.

`deadfish input.wav output.wav -n 0.5`

Set the loudness to -20 LKFS.

`deadfish input.wav output.wav -n -20,k`

### Inverse Filtering

Deadfish supports LPC (Linear Predictive Coding)-based inverse filtering.

Inverse filtering in this case means estimating a filter from each short piece of waveform and apply the filter back to the input to "undo" the filtering. Doing so often reveals some hidden structure in the audio, for example, the glottal excitation pattern when the input is a speech signal.

Perform inverse filtering at a 0.025 seconds interval, using 24th-order LPC.

`deadfish input.wav inverse.wav -I 24,0.025`

The inverse filtering operation also magnifies some audio defects such as clipping and skipping.

### Clip Detection

`deadfish input.wav /dev/null -t 0.999 > clips.txt`

Then view `clips.txt` or load it into Audacity.

### Cascading Operations

Reduce noise -> normalize to 0.3 absolute amplitude -> compress -> normalize again

`deadfish input.wav output.wav -d noise-profile -n 0.3 -c 0.1,0.2 -n 0.3`

Detect audio defects by cascading inverse filtering, normalization and threshold detection.

`deadfish input.wav /dev/null -I 18,0.025 -n 1.0 -t 0.5 > defects.txt`

How to Compile
---

[`ciglet`](https://github.com/Sleepwalking/ciglet) is the only library dependency.

* `cd` into `ciglet`, run `make single-file`. This creates `ciglet.h` and `ciglet.c` under `ciglet/single-file/`. Copy and rename this directory to `deadfish/external/ciglet`.
* Make an empty directory named `build` under `deadfish/`.
* Run `make` from `deadfish/`.

For your information, the directory structure should look like

* `deadfish/`
    * `external/`
        * `ciglet/`
            * `ciglet.h`
            * `ciglet.c`
    * `build/`
    * `deadfish.c`
    * `makefile`

Limitations
---

* Consumes lots of memory on long audio files. This is because the entire audio is loaded and processed at once.
* Only works on mono channel audio. I don't plan to add the support for stereo .wavs. If you want to use deadfish on stereo .wavs, just run it once for each channel and merge the results.
