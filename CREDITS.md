# Credits

## Mutable Instruments Clouds

The granular DSP engine in this project is a direct port of Mutable Instruments
Clouds, designed and implemented by **Émilie Gillet**.

- Original module: https://mutable-instruments.net/modules/clouds/
- Original source code: https://github.com/pichenettes/eurorack/tree/master/clouds
- License: MIT

Émilie Gillet open-sourced the entire Mutable Instruments eurorack codebase.
This project would not exist without that work. Please consider supporting
Mutable Instruments and the open-source hardware/software community.

## What This Port Changed

The Clouds DSP engine (`dsp/`, `stmlib/`, `resources.*`) is used completely
unmodified from the original eurorack repository. The only changes made to
enable compilation on the Daisy platform were:

- Renamed `.cc` source files to `.cpp` for compatibility with the Daisy build system
- Fixed include paths in `resources.cpp` (`clouds/resources.h` → `resources.h`)
- Added `__attribute__((section(".qspiflash_data")))` to lookup tables in
  `resources.cpp` to place them in QSPI flash rather than internal flash

All hardware abstraction, control mapping, audio I/O, LED management, and
pedal-specific features were written from scratch for the Daisy Petal platform
and are original work by the repository author.

## Electro-Smith

The Daisy platform, libDaisy, and DaisySP are the work of Electro-Smith.

- https://electro-smith.com
- https://github.com/electro-smith
