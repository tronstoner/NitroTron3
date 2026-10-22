# Host simulator for the NitroLab armitage module. `make` here (or
# `make -C tools/hostsim` from the repo root) builds ./armsim (offline renderer)
# and ./armlive (real-time + WebSocket control, ui.html).
# Same include layout as tools/host: hardware stubs first, then the pedal
# shell headers, then the shared core; the module compiles unmodified.
ROOT     := ../..
ifeq ($(origin CXX),default)
CXX      := clang++
endif
ifeq ($(origin CC),default)
CC       := clang
endif
CXXFLAGS ?= -O2 -std=c++17 -Wall
CFLAGS   ?= -O2 -w
INCLUDES := -I$(ROOT)/tools/host/stub -I$(ROOT)/pedals/nitrolab \
            -I$(ROOT)/src/core/blocks -I$(ROOT)/src/core/util -I.
MODDEPS  := armitage_host.h host_tunables.h wav.h \
            $(wildcard $(ROOT)/pedals/nitrolab/modules/*.h) $(ROOT)/pedals/nitrolab/constants.h \
            $(ROOT)/pedals/nitrolab/module.h
# armlive is built with the DIAG gate on (CT3_DIAG_BUILD -> NL_DIAG) so the
# module fills its DIAG snapshot; the gate touches Controls() bookkeeping only,
# not the audio path (armlive --selftest vs armsim proves it bit-identical).
LIVEDEFS := -DCT3_DIAG_BUILD
LDLIVE   := -framework CoreFoundation -framework CoreAudio -framework AudioToolbox -lpthread -lm

all: armsim armlive

armsim: armsim.cpp $(MODDEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ armsim.cpp

miniaudio_impl.o: miniaudio_impl.c third_party/miniaudio.h
	$(CC) $(CFLAGS) -c -o $@ miniaudio_impl.c

armlive: armlive.cpp ws_server.h json_mini.h miniaudio_impl.o $(MODDEPS)
	$(CXX) $(CXXFLAGS) $(LIVEDEFS) $(INCLUDES) -o $@ armlive.cpp miniaudio_impl.o $(LDLIVE)

.PHONY: all clean
clean:
	rm -f armsim armlive miniaudio_impl.o
