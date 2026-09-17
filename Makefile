# Project Name
TARGET = NitroTron3

# Uncomment to use LGPL (like ReverbSc, etc.)
#USE_DAISYSP_LGPL=1

# Sources and Hothouse header files
CPP_SOURCES = pedals/$(PEDAL)/main.cpp lib/HothouseExamples/src/hothouse.cpp
C_INCLUDES = -Ipedals/$(PEDAL) -Isrc/core/blocks -Isrc/core/util -Isrc/core/io -Ilib/HothouseExamples/src

# Library Locations
LIBDAISY_DIR = lib/HothouseExamples/libDaisy
DAISYSP_DIR = lib/HothouseExamples/DaisySP

# Pedal selection: default = nitrotron3. `make PEDAL=<name>` builds
# pedals/<name>/main.cpp against the shared core/ library.
PEDAL ?= nitrotron3

# Instrument voicing: default = bass. `make INSTRUMENT=guitar` retunes the
# pitch tracker + frequency voicing constants for electric guitar (see the
# "Instrument profile" block in pedals/<pedal>/constants.h). Make does not track
# -D define changes or a switch of PEDAL source, so a stamp file records the
# PEDAL+INSTRUMENT+DIAG profile of the objects in build/; on mismatch the build dir
# is dropped at parse time (deterministic, safe under -j — a rule-based stamp
# races on make 3.81) and everything recompiles. No manual `make clean` needed.
INSTRUMENT ?= bass
ifeq ($(INSTRUMENT),guitar)
C_DEFS += -DNT3_INSTRUMENT_GUITAR
endif

# Diagnostics build: `make PEDAL=chronotron3 DIAG=1` compiles the opt-in
# instrumentation (USB-serial heartbeat / fault dump, the sprawl non-finite
# guard and its LED2 fault strobe). Default 0 = zero runtime cost: the gates are
# `if (CT3_DIAG)` on a constexpr false, so the code is dead-code eliminated.
# DIAG is part of BUILD_PROFILE below so toggling it forces the full rebuild.
DIAG ?= 0
ifeq ($(DIAG),1)
C_DEFS += -DCT3_DIAG_BUILD
endif
BUILD_PROFILE := $(PEDAL)-$(INSTRUMENT)-diag$(DIAG)
PROFILE_STAMP := build/.buildprofile
LAST_PROFILE := $(strip $(shell cat $(PROFILE_STAMP) 2>/dev/null))
ifneq ($(LAST_PROFILE),$(BUILD_PROFILE))
ifneq ($(LAST_PROFILE),)
$(info Build profile changed ($(LAST_PROFILE) -> $(BUILD_PROFILE)) - full rebuild)
endif
_ := $(shell rm -rf build && mkdir -p build && printf '%s\n' '$(BUILD_PROFILE)' > $(PROFILE_STAMP))
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
