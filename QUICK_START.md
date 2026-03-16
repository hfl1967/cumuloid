# cumuloid — Quick Start Guide

---

### 1. Open the project

Open VS Code, then:
```
File → Open Folder → ~/Documents/Daisy/Projects/cumuloid
```

---

### 2. Open a terminal

```
Terminal → New Terminal  (or Ctrl+`)
```

Make sure you're in the project directory:
```bash
pwd
# should show: .../Daisy/Projects/cumuloid
```

---

### 3. Build

```bash
make
```

A successful build ends with memory usage like:
```
FLASH:       0 GB    128 KB    0.00%
QSPIFLASH:  ~163 KB  7936 KB   ~2%
```

Warnings about `unused-local-typedefs` from stmlib are normal and harmless.

---

### 4. Flash to hardware

**Put Daisy in bootloader mode:**
Double-tap the RESET button on the Daisy Seed quickly.
The LEDs will blink briefly to confirm bootloader mode.

**Then flash:**
```bash
make program-dfu
```

A successful flash ends with:
```
Download done.
File downloaded successfully
Transitioning to dfuMANIFEST state
```

The pedal reboots automatically.

---

### 5. If the flash fails ("No DFU capable USB device")

The bootloader window is short. Try:
```bash
# Run this first, then immediately double-tap RESET
make program-dfu
```

Or if double-tap isn't working, use the factory DFU method:
1. Hold BOOT button on Daisy Seed
2. Press and release RESET while holding BOOT
3. Release BOOT
4. Run `make program-dfu`

Note: The factory method flashes to internal flash only.
For QSPI (required for this project), you need the Daisy bootloader installed.
If you ever need to reinstall the bootloader:
```bash
dfu-util -a 0 -s 0x08000000:leave \
  -D ../../libDaisy/core/dsy_bootloader_v6_2-intdfu-2000ms.bin \
  -d ,0483:df11
```

---

### 6. Git workflow

```bash
# Check what's changed
git status
git diff cumuloid.cpp

# Save a milestone
git add cumuloid.cpp
git commit -m "describe what you changed"

# Push to GitHub
git push

# Roll back to last commit if something breaks
git checkout cumuloid.cpp
```

---

### 7. Key files

| File | Purpose |
|------|---------|
| `cumuloid.cpp` | Main file — all hardware control + audio callback |
| `Makefile` | Build config — APP_TYPE, source files, includes |
| `dsp/` | Clouds DSP engine (don't edit) |
| `stmlib/` | stmlib support library (don't edit) |
| `resources.cpp/h` | Clouds lookup tables (don't edit) |

---

### 8. Current control map (quick reference)

**Knobs (normal mode):** K1=Position · K2=Size · K3=Texture · K4=Density · K5=Pitch · K6=Blend

**Encoder turn:** cycle modes (Amber/Cyan/Green/Purple)
**Encoder hold (shift):** K1=Reverb · K2=Feedback · K3=LP cutoff · K4=HP cutoff · K5=Dry level · K6=Output level

**Footswitches:** S1=Freeze · S2=Quality cycle · S3=Bypass · S4=Tap tempo (hold to clear)

**Microswitches:** S5=Stereo spread · S6=Randomize grains · S7=Pitch snap