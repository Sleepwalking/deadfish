DEADFISH
===

A very simple .wav noise reduction tool. I wrote this tool because the algorithm implemented by SoX has some flaws in the short-time Fourier transform, and fixing it is likely to take more lines than rewriting one.

LICENSE: BSD

> How important is impedance matching in audio applications?

> The answers depend on whether you're a electrical engineer or audiophoole. If the latter, we can mumble on at length about oxygen free cables, exra-phancy capacitors, and lots of other expensive nonsense you must follow while waving a dead fish over your amplifier during a full moon. -- Olin Lathrop

> https://electronics.stackexchange.com/questions/6846/how-important-is-impedance-matching-in-audio-applications/6854

How to Use
---

###An example

First generate a noise profile from a .wav file that contains only noisy samples.

`deadfish noise.wav -a noise-profile`

Then apply the noise profile on to the .wav file you want to denoise.

`deadfish input.wav output.wav -d noise-profile`

The degree/rate of noise reduction can be controlled using option `-r`. The default is `-r 1.0`.

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
