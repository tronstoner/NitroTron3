# Web Firmware Updater

A browser-based flasher so pedal owners can update without installing
`dfu-util`: connect USB, enter DFU mode, pick a firmware version, flash.
Lives in `docs/updater/`, styled after [nitromahalia.net](https://nitromahalia.net/).

## How it works

- **WebUSB + DFU.** The STM32H750's ROM bootloader (VID `0x0483` / PID
  `0xDF11`) speaks standard USB DFU. The page drives it with the vendored
  [devanlai/webdfu](https://github.com/devanlai/webdfu) library (`dfu.js` /
  `dfuse.js`, ISC — `LICENSE-webdfu.txt`), writing to internal flash at
  `0x08000000` (DfuSe erase → program → manifest, i.e. `dfu-util -s
  0x08000000:leave`).
- **DFU entry** is the pedal's existing gesture: hold FS1+FS2 for 2 s.
- **Firmware images are served same-origin** from
  `docs/updater/firmware/` + `manifest.json`. Verified finding (2026-07):
  GitHub release-asset downloads have **no CORS headers** on the final CDN
  response (`release-assets.githubusercontent.com`), so a page cannot fetch
  them cross-origin. The manifest lists version / variant (bass, guitar) /
  file / sha256 / release-notes link; the page verifies the sha256
  (WebCrypto) before flashing. A local-`.bin` file picker is the fallback
  and works with no manifest at all.
- **Safety:** the ROM bootloader cannot be overwritten by this path — a
  failed flash is recovered by re-entering DFU. The page refuses images
  larger than the 128 KB internal flash.

## Files

```
docs/updater/
├── index.html           # the page
├── updater.css          # nitromahalia.net-derived styles
├── app.js               # connect / manifest / verify / flash logic
├── dfu.js, dfuse.js     # vendored webdfu (ISC)
├── LICENSE-webdfu.txt
└── firmware/
    ├── manifest.json    # flash_address, flash_size, firmware[] entries
    └── NitroTron3-*.bin # the flashable images (~128 KB each)
```

## Publishing

Deployment is workflow-based: `.github/workflows/deploy-pages.yml` publishes
the `docs/` folder to GitHub Pages on every push to `main` that touches
`docs/**` (manual runs via the Actions tab also work). One-time setup: repo
Settings → Pages → Source: **GitHub Actions**. The updater is then at:

    https://tronstoner.github.io/NitroTron3/updater/

(Pages publishes all of `docs/` — specs and manual included, which is fine,
they're public in the repo anyway.) WebUSB requires a secure context; Pages
is https, so nothing more to do. Local testing: `python3 -m http.server` —
WebUSB treats `localhost` as secure.

## Release process addition

When cutting a release (see `.agents/skills/release/`), additionally:

1. Copy the release `.bin`(s) into `docs/updater/firmware/`
   (bass and, when shipped, guitar variants).
2. Prepend a matching entry (version, variant, file, sha256, released,
   notes URL) to `firmware/manifest.json` — newest first; the page
   pre-selects the first entry.

The release skill should gain these steps when it is next touched.

## Constraints & caveats

- **Browsers:** Chrome / Edge / Opera (desktop + Android). Safari and
  Firefox have declined to implement WebUSB — the page shows a notice.
- **Windows:** some machines need WinUSB bound to "STM32 BOOTLOADER" via
  Zadig — same friction as `dfu-util`, noted on the page.
- **QSPI migration:** the firmware currently targets internal flash, so
  plain DfuSe at `0x08000000` is sufficient. If the planned QSPI linker
  swap happens, flashing must go through the Daisy bootloader at a
  different address — revisit the page (and manifest `flash_address`)
  then.
