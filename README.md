# NitroTron3

A DIY digital bass effects pedal built on the Electro-Smith Daisy Seed and Cleveland Music Co. Hothouse DSP kit.

## Hardware

- Daisy Seed (STM32H750, 480 MHz Cortex-M7, 32-bit float, 48 kHz audio)
- Hothouse DSP Pedal Kit (6 knobs, 3x 3-position toggles, 2 footswitches, LED, true-bypass relay)
- Hammond 125B enclosure

## Repository setup

This repo uses [HothouseExamples](https://github.com/clevelandmusicco/HothouseExamples) as a single git submodule under `lib/`. That submodule in turn contains libDaisy and DaisySP as nested submodules, so one checkout gets the full toolchain.

```
NitroTron3/
├── NitroTron3.cpp              # main source
├── Makefile
├── lib/
│   └── HothouseExamples/       # submodule
│       ├── libDaisy/            # nested submodule — hardware abstraction
│       ├── DaisySP/             # nested submodule — DSP library
│       └── src/
│           ├── hothouse.h       # Hothouse hardware proxy (compiled by our Makefile)
│           └── hothouse.cpp
└── .vscode/                    # build/debug tasks, IntelliSense config
```

## Getting started

Clone with submodules:

```sh
git clone --recurse-submodules <repo-url>
cd NitroTron3
```

Build the libraries (one-time):

```sh
make -C lib/HothouseExamples/libDaisy
make -C lib/HothouseExamples/DaisySP
```

Build and flash:

```sh
make            # compile
make program    # flash via OpenOCD / ST-Link
make program-dfu  # flash via DFU bootloader
```

## Updating dependencies

```sh
cd lib/HothouseExamples
git pull
git submodule update --recursive
cd ../..
make -C lib/HothouseExamples/libDaisy clean && make -C lib/HothouseExamples/libDaisy
make -C lib/HothouseExamples/DaisySP clean && make -C lib/HothouseExamples/DaisySP
```
