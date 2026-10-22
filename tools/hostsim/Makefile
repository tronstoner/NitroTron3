# Host simulator for the NitroLab armitage module. `make` here (or
# `make -C tools/hostsim` from the repo root) builds ./armsim.
# Same include layout as tools/host: hardware stubs first, then the pedal
# shell headers, then the shared core; the module compiles unmodified.
ROOT     := ../..
CXX      ?= clang++
CXXFLAGS ?= -O2 -std=c++17 -Wall
INCLUDES := -I$(ROOT)/tools/host/stub -I$(ROOT)/pedals/nitrolab \
            -I$(ROOT)/src/core/blocks -I$(ROOT)/src/core/util -I.
DEPS     := armsim.cpp armitage_host.h host_tunables.h wav.h \
            $(wildcard $(ROOT)/pedals/nitrolab/modules/*.h) $(ROOT)/pedals/nitrolab/constants.h \
            $(ROOT)/pedals/nitrolab/module.h

armsim: $(DEPS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ armsim.cpp

.PHONY: clean
clean:
	rm -f armsim
