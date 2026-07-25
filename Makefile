# Project Name
TARGET = NitroTron3

# Uncomment to use LGPL (like ReverbSc, etc.)
#USE_DAISYSP_LGPL=1

# Sources and Hothouse header files
CPP_SOURCES = src/NitroTron3.cpp lib/HothouseExamples/src/hothouse.cpp
C_INCLUDES = -Isrc -Isrc/core/blocks -Isrc/core/util -Isrc/core/io -Ilib/HothouseExamples/src

# Library Locations
LIBDAISY_DIR = lib/HothouseExamples/libDaisy
DAISYSP_DIR = lib/HothouseExamples/DaisySP

# Instrument voicing: default = bass. `make INSTRUMENT=guitar` retunes the
# pitch tracker + frequency voicing constants for electric guitar (see the
# "Instrument profile" block in src/constants.h). Make does not track -D
# define changes, so a stamp file records the profile of the objects in
# build/; on mismatch the build dir is dropped at parse time (deterministic,
# safe under -j — a rule-based stamp races on make 3.81) and everything
# recompiles with the right defines. No manual `make clean` needed.
INSTRUMENT ?= bass
ifeq ($(INSTRUMENT),guitar)
C_DEFS += -DNT3_INSTRUMENT_GUITAR
endif
INSTRUMENT_STAMP := build/.instrument
LAST_INSTRUMENT := $(strip $(shell cat $(INSTRUMENT_STAMP) 2>/dev/null))
ifneq ($(LAST_INSTRUMENT),$(INSTRUMENT))
ifneq ($(LAST_INSTRUMENT),)
$(info Instrument profile changed ($(LAST_INSTRUMENT) -> $(INSTRUMENT)) - full rebuild)
endif
_ := $(shell rm -rf build && mkdir -p build && printf '%s\n' '$(INSTRUMENT)' > $(INSTRUMENT_STAMP))
endif

# Run the app from RAM via the Daisy bootloader: internal flash (128 KB) holds
# only the bootloader; the app lives in QSPI and is copied to RAM at boot.
# One-time setup per Seed: enter DFU mode, then `make program-boot`.
# After that, `make program-dfu` flashes the app through the bootloader
# (it must also be in DFU/bootloader state to receive the app).
APP_TYPE = BOOT_SRAM

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# ---------------------------------------------------------------------------
# User manual (PDF)
# ---------------------------------------------------------------------------
# Requires: pandoc + weasyprint
#   brew install pandoc weasyprint
.PHONY: manual
manual: docs/USER_MANUAL.pdf

docs/assets/pedal-mode-a.svg docs/assets/pedal-mode-b.svg docs/assets/pedal-mode-c.svg &: docs/gen_layout_svg.py
	python3 docs/gen_layout_svg.py

ICON_SVGS = docs/assets/icon-ccw.svg docs/assets/icon-cw.svg docs/assets/icon-noon.svg \
            docs/assets/icon-uni.svg docs/assets/icon-steps.svg docs/assets/icon-bipolar.svg

$(ICON_SVGS) &: docs/gen_icons.py
	python3 docs/gen_icons.py

docs/USER_MANUAL.pdf: docs/USER_MANUAL.md docs/manual.css \
                     docs/assets/pedal-mode-a.svg docs/assets/pedal-mode-b.svg docs/assets/pedal-mode-c.svg \
                     $(ICON_SVGS)
	pandoc $< \
	  --from gfm+attributes \
	  --pdf-engine=weasyprint \
	  --css=docs/manual.css \
	  --resource-path=docs \
	  --standalone \
	  --metadata pagetitle="NitroTron3 — User Manual" \
	  -o $@
	@echo "Wrote $@"
