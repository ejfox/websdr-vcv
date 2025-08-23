# WebSDR Plugin for Cardinal/VCV Rack
RACK_DIR ?= ../Rack-SDK

# Plugin details
SLUG = WebSDR
VERSION = 0.1.0

# Determine platform
include $(RACK_DIR)/arch.mk

# Plugin name
NAME = WebSDR

# Sources
SOURCES += src/plugin.cpp
SOURCES += src/modules/WebSDRModule.cpp
SOURCES += src/modules/SpectrumAnalyzer.cpp
SOURCES += src/network/WebSDRClient.cpp

# Compiler flags
FLAGS += -I./src
FLAGS += -I./deps/include
FLAGS += -std=c++11
FLAGS += -DVERSION=$(VERSION)

# Platform-specific flags
ifeq ($(ARCH_WIN),true)
	LDFLAGS += -lws2_32 -lwinmm
endif

ifeq ($(ARCH_MAC),true)
	LDFLAGS += -framework CoreAudio -framework AudioToolbox
endif

ifeq ($(ARCH_LIN),true)
	LDFLAGS += -lpthread -lrt
endif

# Optimization flags for audio processing
FLAGS += -O2

# Include VCV Rack build system
include $(RACK_DIR)/plugin.mk

# Custom targets
.PHONY: prepare
prepare:
	@echo "Preparing build environment..."
	@mkdir -p build
	@mkdir -p dist

.PHONY: clean-all
clean-all: clean
	@rm -rf build
	@rm -rf dist
	@rm -rf deps/build

.PHONY: test
test:
	@echo "Running tests..."
	@cd test && $(MAKE) test

.PHONY: format
format:
	@echo "Formatting code..."
	@clang-format -i src/**/*.cpp src/**/*.hpp

# Distribution target
dist: all
	@mkdir -p dist/$(SLUG)
	@cp -R res dist/$(SLUG)/
	@cp plugin.json dist/$(SLUG)/
	@cp LICENSE dist/$(SLUG)/
	@cp README.md dist/$(SLUG)/
	@cp plugin.$(PLUGIN_EXT) dist/$(SLUG)/
	@cd dist && zip -r $(SLUG)-$(VERSION)-$(ARCH_NAME).zip $(SLUG)