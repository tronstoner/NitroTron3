# Installing NitroTron3 firmware

NitroTron3 ships as a `.bin` file that you flash to the Daisy Seed inside the Hothouse pedal over USB. Two flashing paths are supported: `dfu-util` from the command line, or the Electrosmith Web Programmer in a Chromium-based browser.

## 0. One-time setup: the Daisy bootloader (required since v0.5)

Since v0.5 the firmware is larger than the STM32's 128 KB internal flash and
runs via the **Electro-Smith Daisy bootloader**: the bootloader lives in
internal flash, the firmware lives in the Seed's QSPI flash and is loaded to
RAM at boot. The flash address changed accordingly — `0x90040000` (QSPI), not
`0x08000000`.

Each pedal needs the Daisy bootloader installed **once**:

1. Enter DFU mode. Coming from v0.4 or older: hold both footswitches 2 s
   (that firmware enters the STM32 ROM bootloader). Fresh/blank Seed: use the
   BOOT/RESET buttons (see Fallback below).
2. Flash the bootloader — easiest via the
   [Electrosmith Web Programmer](https://electro-smith.github.io/Programmer/):
   **Connect**, then use its built-in **Flash Bootloader Image** option.
   (From a source checkout, `make program-boot` does the same.)
3. Power-cycle the pedal. With no app installed yet, the bootloader just
   waits in DFU mode (Seed LED pulses) — continue with flashing below.

From v0.5 firmware onward, the both-footswitch hold jumps straight into the
Daisy bootloader, which waits in DFU indefinitely — the day-to-day flashing
feel is unchanged.

## 1. Put the pedal into DFU bootloader mode

**Hold both footswitches (FS1 + FS2) for 2 seconds.** The LEDs alternate rapidly for ~1.2 s as confirmation, then the pedal disconnects from audio and re-enumerates over USB as `STM Device in DFU Mode`.

> **Note for users coming from other Hothouse pedals.** Most Hothouse examples use FS1 alone (held 2 s) to enter DFU. NitroTron3 has dropped the FS1-alone path because it's too easy to trigger by accident while preset-cycling. Only the both-FS hold counts here.

If you already have a USB cable plugged in when you do the hold, it will reconnect as a DFU device — no need to unplug.

### Fallback: BOOT/RESET buttons on the Daisy Seed

If the firmware is unresponsive (bricked, never flashed, or you're flashing the very first time), use the hardware buttons on the Daisy Seed module itself. They're on the Daisy Seed board inside the Hothouse — you need to open the enclosure to reach them.

1. Hold the **BOOT** button.
2. Press and release the **RESET** button (while still holding BOOT).
3. Release **BOOT**.

The pedal enumerates as `STM Device in DFU Mode`. This path works regardless of firmware state and is the standard Daisy recovery route — see the [Daisy Ecosystem Wiki](https://github.com/electro-smith/DaisyWiki/wiki) for context.

> Note: BOOT/RESET lands you in the STM32 **ROM** bootloader, which can only
> write internal flash — use it to (re)install the Daisy bootloader (section
> 0), not to flash the firmware itself. After installing the bootloader,
> power-cycle and flash the firmware at `0x90040000` while the bootloader
> waits in DFU (it waits indefinitely when no app is installed; ~2 s after
> power-on when one is).

## 2. Flash the binary

### Option A — `dfu-util` (command line)

Install once:

```sh
# macOS
brew install dfu-util

# Debian / Ubuntu
sudo apt install dfu-util
```

Flash (note the QSPI address — the pedal must be in the **Daisy** bootloader,
see section 0):

```sh
dfu-util -a 0 -s 0x90040000:leave -D NitroTron3-vX.Y.bin
```

The pedal reboots into normal operation when flashing finishes.

### Option B — Electrosmith Web Programmer

1. Open [the Electrosmith Web Programmer](https://electro-smith.github.io/Programmer/) in Chrome or Edge.
2. Click **Connect**, select the DFU device.
3. Set the start address to `0x90040000` (QSPI — required since v0.5).
4. Choose the `NitroTron3-vX.Y.bin` file you downloaded.
5. Click **Program**. Wait for "Done."

## 3. First-boot behaviour

- On first boot of a new firmware version, the pedal starts in Manual mode and reads current knob/switch positions into the edit buffer. LED 1 is off (Manual).
- If you are upgrading from an older NitroTron3 firmware (the per-mode preset layout, before banks), your existing presets are migrated automatically: Mode A's slots → Bank 1, Mode B's slots → Bank 2, Mode C's slots → Bank 3. No data loss.
- If you're switching from a completely different firmware (a Hothouse example, etc.), the flash layout doesn't match and the pedal will factory-reset to empty presets on first boot.

## 4. Verifying the install

- Plug your bass in, plug into an amp. Make sure FS2 is engaged (LED 2 solid on, true-bypass relay disengaged).
- Wiggle each knob — you should hear the parameter respond live (manual mode is WYSIWYG).
- Flip SW3: the audio character changes mode (Bordun / Sprawl / Schism).
- Press FS1 short — LED 1 should start blinking I (Preset 1 pattern). FS1 long-press jumps back to manual.

See `docs/USER_MANUAL.pdf` (or the `USER_MANUAL.pdf` attached to the GitHub release) for the full control reference.

## 5. Source code

NitroTron3 is licensed under GPL v3. Every binary release is tagged in the repository — `Source code (zip)` and `Source code (tar.gz)` are auto-generated next to the `.bin` on each release page, and the tag itself can be checked out:

```sh
git clone --recurse-submodules https://github.com/<owner>/NitroTron3.git
cd NitroTron3
git checkout vX.Y
```

See `README.md` for the build toolchain (pandoc + weasyprint for the manual; `arm-none-eabi-g++` via the libDaisy submodule for firmware), and `THIRD_PARTY_LICENSES.md` in the release for the license texts of all bundled code (libDaisy, DaisySP, Hothouse, MI Clouds reverb).
