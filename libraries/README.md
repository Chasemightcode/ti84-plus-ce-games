# CE C libraries

`clibs.8xg` is the library bundle that C programs for the TI-84 Plus CE
load at run time through LibLoad: GraphX (graphics), KeypadC (keys),
FileIOC (files), FontLibC, and the USB/serial/FAT drivers. Both games need
it. Send it to the calculator once; every C game shares it.

It is the unmodified release from
[CE-Programming/libraries](https://github.com/CE-Programming/libraries/releases),
the version these games were built and tested against (CE C toolchain
v15). A newer release works too. It is © Matthew Waltz and the
CE-Programming contributors and is redistributed under the BSD-2-Clause
license in [LICENSE](LICENSE).
