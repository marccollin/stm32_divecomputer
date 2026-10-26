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
└── include.zip     # Archived headers
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
