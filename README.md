# cumuloid — Mutable Instruments Clouds on Daisy Petal

A full port of Mutable Instruments Clouds granular processor to the Electro-Smith Daisy Petal guitar pedal platform.

---

## What This Is

Clouds is a legendary granular audio processor originally designed for Eurorack modular synthesizers by Mutable Instruments (Émilie Gillet). cumuloid brings the full Clouds DSP engine — including all four processing modes (Granular, Spectral Stretch, Looping Delay, and Oliverb/Spectral) — to the Daisy Petal, a guitar pedal platform built around the STM32H750 microcontroller.

---

## Controls

### Knobs
| Knob | Normal Mode | Shift Mode (encoder held) |
|------|-------------|--------------------------|
| K1 | Position | Reverb amount |
| K2 | Size (grain length) | Feedback amount |
| K3 | Texture | — |
| K4 | Density | — |
| K5 | Pitch (smooth or interval snap) | — |
| K6 | Blend (dry/wet) | Output level |

Shift mode uses **catch behavior**: a knob only takes effect once it passes the previously set value, preventing jumps. On shift release, the primary knob is similarly ignored until it passes its exit position.

### Encoder
- **Turn** → cycle through the 4 processing modes
- **Hold** → enter shift mode (enables secondary knob functions above)

### Microswitches
| Switch | Up | Down |
|--------|-----|------|
| S5 (SW_5) | Stereo spread on | Stereo spread off |
| S6 (SW_6) | Randomized grain positions | Locked/deterministic grain positions |
| S7 (SW_7) | Pitch snaps to intervals | Pitch is smooth/continuous |

Pitch snap intervals: -24, -12, -7, 0, +7, +12, +24 semitones (2 octaves down to 2 octaves up, with octaves and fifths).

### Footswitches
| Footswitch | Function | LED |
|------------|----------|-----|
| S1 (SW_1) | Freeze (toggle) | Lit when frozen |
| S2 (SW_2) | Quality cycle (16-bit stereo → mono → 8-bit stereo → mono) | Brightness shows level: full / 66% / 33% / 10% |
| S3 (SW_3) | Bypass (toggle) | Lit when effect engaged |
| S4 (SW_4) | Tap tempo → grain size | Blinks at tapped rate |

**S4 hold** (1 second): clears tap tempo and returns grain size to K2.

### Ring LEDs
- Shows current processing mode by color:
  - **Amber** = Granular
  - **Cyan** = Stretch
  - **Green** = Looping
  - **Purple** = Spectral
- **Blue tint** when freeze is active
- **Dimmed to 30%** when shift mode is held

---

## Processing Modes (cycle with encoder)

1. **Granular** — classic granular synthesis. Grains of audio scattered across a buffer.
2. **Stretch** — time stretching / pitch shifting via WSOLA.
3. **Looping Delay** — looping sample player with granular control.
4. **Spectral (Oliverb)** — FFT-based spectral processing, phase vocoder effects.

---

## Build & Flash Instructions

### Prerequisites

1. **ARM GCC toolchain** (arm-none-eabi-gcc 10.3+)
   - Mac: install via [Homebrew](https://brew.sh): `brew install --cask gcc-arm-embedded`
   - Or download from [ARM developer site](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain)

2. **libDaisy + DaisySP + DaisyExamples**
   ```bash
   cd ~/Documents/Daisy
   git clone https://github.com/electro-smith/DaisyExamples.git
   cd DaisyExamples
   git submodule update --init --recursive
   ```
   Build libDaisy and DaisySP:
   ```bash
   cd libDaisy && make && cd ..
   cd DaisySP && make && cd ..
   ```

3. **dfu-util**
   - Mac: `brew install dfu-util`
   - Linux: `sudo apt install dfu-util`

4. **Mutable Instruments eurorack repo**
   ```bash
   cd ~/Documents/Daisy
   git clone https://github.com/pichenettes/eurorack.git
   cd eurorack
   git submodule update --init --recursive
   ```

---

### Project Setup

1. **Clone or copy this project** into your DaisyExamples petal folder:
   ```bash
   cp -r cumuloid ~/Documents/Daisy/DaisyExamples/petal/cumuloid
   cd ~/Documents/Daisy/DaisyExamples/petal/cumuloid
   ```

2. **Copy Clouds DSP sources** from the eurorack repo:
   ```bash
   EURORACK=~/Documents/Daisy/eurorack
   PROJECT=~/Documents/Daisy/DaisyExamples/petal/cumuloid

   # Create directories
   mkdir -p $PROJECT/dsp/fx $PROJECT/dsp/pvoc
   mkdir -p $PROJECT/stmlib/dsp $PROJECT/stmlib/utils $PROJECT/stmlib/fft

   # Clouds DSP
   cp $EURORACK/clouds/dsp/*.h $PROJECT/dsp/
   cp $EURORACK/clouds/dsp/*.cc $PROJECT/dsp/
   cp $EURORACK/clouds/dsp/fx/*.h $PROJECT/dsp/fx/
   cp $EURORACK/clouds/dsp/pvoc/*.h $PROJECT/dsp/pvoc/
   cp $EURORACK/clouds/dsp/pvoc/*.cc $PROJECT/dsp/pvoc/

   # Clouds resources
   cp $EURORACK/clouds/resources.h $PROJECT/
   cp $EURORACK/clouds/resources.cc $PROJECT/resources.cpp

   # stmlib dependencies
   cp $EURORACK/stmlib/stmlib.h $PROJECT/stmlib/
   cp $EURORACK/stmlib/dsp/*.h $PROJECT/stmlib/dsp/
   cp $EURORACK/stmlib/dsp/units.cc $PROJECT/stmlib/dsp/units.cpp
   cp $EURORACK/stmlib/dsp/atan.cc $PROJECT/stmlib/dsp/atan.cpp
   cp $EURORACK/stmlib/utils/*.h $PROJECT/stmlib/utils/
   cp $EURORACK/stmlib/utils/random.cc $PROJECT/stmlib/utils/random.cpp
   cp $EURORACK/stmlib/fft/* $PROJECT/stmlib/fft/
   ```

3. **Fix include paths** in resources.cpp:
   ```bash
   sed -i '' 's|#include "clouds/resources.h"|#include "resources.h"|g' resources.cpp
   ```

4. **Rename .cc files to .cpp** (the Daisy build system only handles .cpp):
   ```bash
   for f in dsp/*.cc dsp/pvoc/*.cc; do mv "$f" "${f%.cc}.cpp"; done
   ```

---

### Build

```bash
cd ~/Documents/Daisy/DaisyExamples/petal/cumuloid
make
```

A successful build shows:
```
FLASH:          0 GB       128 KB      0.00%
SDRAM:        ~180 KB        64 MB
QSPIFLASH:    ~163 KB      7936 KB      ~2%
```

---

### Flash to Hardware

#### One-time: Install the Daisy Bootloader

This only needs to be done once per Daisy Seed. It enables QSPI flash programming via USB.

1. Put Daisy in factory DFU mode: hold **BOOT** button, press/release **RESET**, release **BOOT**
2. Flash the bootloader:
   ```bash
   dfu-util -a 0 -s 0x08000000:leave \
     -D ../../libDaisy/core/dsy_bootloader_v6_2-intdfu-2000ms.bin \
     -d ,0483:df11
   ```

#### Flash the cumuloid firmware

1. **Double-tap the RESET button** quickly — the LEDs will blink briefly to confirm bootloader mode
2. Run:
   ```bash
   make program-dfu
   ```

A successful flash shows:
```
Download done.
File downloaded successfully
Transitioning to dfuMANIFEST state
```

The pedal boots automatically after flashing.

---

## Project Structure

```
cumuloid/
├── cumuloid.cpp            # Main file — all hardware abstraction + audio callback
├── Makefile                # Build config (APP_TYPE = BOOT_QSPI)
├── resources.cpp           # Clouds lookup tables (from eurorack repo)
├── resources.h             # Clouds resources header
├── dsp/                    # Clouds DSP engine (from eurorack repo)
│   ├── granular_processor.cpp/h
│   ├── correlator.cpp/h
│   ├── mu_law.cpp/h
│   ├── parameters.h
│   ├── frame.h
│   ├── audio_buffer.h
│   ├── grain.h
│   ├── granular_sample_player.h
│   ├── looping_sample_player.h
│   ├── wsola_sample_player.h
│   ├── sample_rate_converter.h
│   ├── window.h
│   ├── fx/
│   │   ├── diffuser.h
│   │   ├── fx_engine.h
│   │   ├── pitch_shifter.h
│   │   └── reverb.h
│   └── pvoc/
│       ├── frame_transformation.cpp/h
│       ├── phase_vocoder.cpp/h
│       └── stft.cpp/h
└── stmlib/                 # stmlib support library (from eurorack repo)
    ├── stmlib.h
    ├── dsp/
    │   ├── units.cpp/h
    │   ├── atan.cpp/h
    │   ├── filter.h
    │   ├── dsp.h
    │   └── ...
    ├── fft/
    │   └── shy_fft.h
    └── utils/
        ├── random.cpp/h
        └── ...
```

---

## Technical Notes

### Memory Layout
The Daisy Seed has 128KB of internal flash, 64MB of SDRAM, and 8MB of QSPI flash. This project uses `APP_TYPE = BOOT_QSPI` which places the entire program in QSPI flash (accessed as memory-mapped storage at 0x90040000), leaving internal flash for the bootloader only.

- **QSPI flash**: program code + Clouds lookup tables (~163KB)
- **SDRAM**: grain buffers (118KB large + 64KB small = ~182KB)

### Single Translation Unit Build
Rather than fighting with the Daisy build system's handling of subdirectory `.cpp` files, all Clouds DSP sources are `#include`d directly into `cumuloid.cpp`. This is a standard embedded technique that also allows the compiler to optimize across all DSP code in a single pass.

### Clouds Internal Sample Rate
Clouds runs its DSP internally at 32kHz (or 16kHz in lo-fi mode), with a built-in sample rate converter. The Daisy runs at 48kHz. The SRC in Clouds handles the conversion automatically.

---

## License

Clouds DSP engine: Copyright 2014 Émilie Gillet, licensed CC BY-SA 3.0
cumuloid port: see LICENSE file
