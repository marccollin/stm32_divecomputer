# STM32 Dive Computer

Experimental dive-computer firmware for an **STM32F407**. This project targets ARM Cortex-M4.
The firmware reads pressure and temperature, tracks dives, and displays live dive information.
It also estimates no-decompression limits and decompression stops using a Buhlmann ZHL-16C model with gradient factors.

**Safety notice:** This project is experimental and has not been validated or certified for diving.
Do not rely on it for dive planning or as a primary or backup dive computer.

## Features

- MS5803-05BA pressure and temperature sensor support.
- Dive detection and display of depth, maximum depth, dive time, NDL, ceiling, and ascent plan.
- Buhlmann ZHL-16C tissue model with gradient-factor decompression estimates.
- Air, nitrox, and trimix support in the gas model.
- SD-card storage of tissue state.
- Completed-dive log with maximum and average depth, duration, and UTC start/end times.
- Surface breaks shorter than five minutes merge into the same dive. A sample below 0.8 m
  starts the window; descending past 1.2 m resumes it. At five minutes the dive closes
  with its original surfacing timestamp, excluding the final wait from duration/average depth.
  Intermediate breaks remain part of a merged dive. Pending state is saved on transitions
  and every minute; after restart, elapsed RTC time advances an already pending surface window.
  Unknown/negative elapsed time does not advance it. Unobserved underwater activity cannot be recovered.
- Latched `MISSED DECO` warning when a sample is more than 1 metre above the model's required
  ceiling (40 m ceiling: 39 m is tolerated, shallower than 39 m triggers the warning).
  This warning tolerance does not change the required ceiling or ascent plan.
  The flag is saved immediately with tissues, survives restart, and is retained in completed-dive
  history (`dives.csv`, sixth column: 0/1). The live warning resets at the next dive, not when
  the ceiling clears. Legacy five-column logs and version-1 tissue files remain readable,
  but contain no recorded violation information. Detection is sample-based and cannot recover
  violations while the computer is off. This is not a flight-clearance or medical assessment.
- Three-button menus, demo screens, and clock, units, water-type, language, gas, and decompression settings.
- 128×64 display interface and low-power idle behavior at the surface.

## Repository structure

```text
├── include/        # Header files for the firmware
├── kicad/          # KiCad hardware schematics and PCB design
├── lib/            # External/third-party libraries
├── src/            # Firmware source files
├── system/         # System-level configuration and utilities
├── Dockerfile      # Containerized build environment
├── SConstruct      # SCons build configuration (main)
├── SConscript      # SCons build configuration
├── setup           # Environment setup script
```

## Getting Started

The project uses SCons and the GNU Arm Embedded toolchain. The Dockerfile extends
`wischner/gcc-arm-none-eabi:1.2.0` with SCons; that base image also provides OpenOCD.
Run these commands from a Linux host with Docker installed:

```sh
git clone https://github.com/marccollin/stm32_divecomputer.git
cd stm32_divecomputer

docker pull wischner/gcc-arm-none-eabi:1.2.0
docker build -t stm32_divecomputer .
```

To build the firmware without flashing:

```sh
docker run --rm \
  -v "$PWD":/work \
  -w /work \
  stm32_divecomputer scons
```

To build and flash it, connect a compatible debug probe and run:

```sh
docker run --rm \
  --device-cgroup-rule='c 189:* rmw' \
  -v /dev/bus/usb:/dev/bus/usb \
  -v "$PWD":/work \
  -w /work \
  stm32_divecomputer scons upload
```


## Contributing

Pull requests and issues are welcome. Please keep changes focused and document new features in the source comments and this README.
