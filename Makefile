# Project Name
TARGET = NitroTron3

# Uncomment to use LGPL (like ReverbSc, etc.)
#USE_DAISYSP_LGPL=1

# Sources and Hothouse header files
CPP_SOURCES = src/NitroTron3.cpp lib/HothouseExamples/src/hothouse.cpp
C_INCLUDES = -Isrc -Ilib/HothouseExamples/src

# Library Locations
LIBDAISY_DIR = lib/HothouseExamples/libDaisy
DAISYSP_DIR = lib/HothouseExamples/DaisySP

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
