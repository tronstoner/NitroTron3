---
name: build
description: Compile the NitroTron3 firmware and report success/failure concisely.
disable-model-invocation: false
allowed-tools: Bash(make *)
---

# Build NitroTron3

Compile the firmware. Report only errors, warnings, memory usage, and success/failure.

## Steps

1. Run `make 2>&1 | tail -20`
2. If errors: show the first error and suggest a fix
3. If clean: report FLASH usage percentage and confirm "Ready to flash"

## First-time setup

If dependencies haven't been built:
```bash
make -C lib/HothouseExamples/libDaisy
make -C lib/HothouseExamples/DaisySP
```
