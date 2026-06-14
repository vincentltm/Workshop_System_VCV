# MTM Workshop System VCV Rack Plugin
# Based on the Workshop_Computer_VCV build system

RACK_DIR ?= dep/Rack-SDK

# Plugin sources
SOURCES += src/plugin.cpp
SOURCES += src/WorkshopSystem.cpp
SOURCES += src/Pedalboard.cpp
SOURCES += deps/Workshop_Computer_VCV/src/cards/CardRegistry.cpp

# Flags
FLAGS += -std=c++17 -fPIC -I./src -Ideps/Workshop_Computer_VCV/src -DCOMPUTERCARD_NOIMPL -DVCV_PORT=1

# Name of the plugin
PLUGIN_NAME = MTMWorkshopSystem

# Target to copy cards and web resources from Workshop_Computer_VCV submodule
copy-cards:
	@echo "Building Workshop_Computer_VCV submodule first..."
	$(MAKE) -C deps/Workshop_Computer_VCV RACK_DIR=$(abspath $(RACK_DIR))
	@echo "Copying card libraries and web resources from Workshop_Computer_VCV submodule..."
	@mkdir -p res/cards
	@cp -R deps/Workshop_Computer_VCV/res/cards/ res/cards/
	@mkdir -p res/web
	@cp -R deps/Workshop_Computer_VCV/res/web/ res/web/
	@mkdir -p res/backyard_rain
	@cp -R deps/Workshop_Computer_VCV/res/backyard_rain/ res/backyard_rain/
	@mkdir -p res/wav
	@cp -R deps/Workshop_Computer_VCV/res/wav/ res/wav/
	@cp deps/Workshop_Computer_VCV/res/vcv_bridge.js res/vcv_bridge.js

# Distributable assets
DISTRIBUTABLES += res
DISTRIBUTABLES += plugin.json

include $(RACK_DIR)/plugin.mk

# Link ws2_32 on Windows for the web server
ifdef ARCH_WIN
    LDFLAGS += -lws2_32
endif

$(TARGET): | copy-cards

# Extra install rule pointing to the VCV Rack 2 plugin dir
RACK2_PLUGIN_DIR = $(HOME)/Library/Application\ Support/Rack2/plugins-mac-arm64

.PHONY: copy-cards clean-submodule

clean: clean-submodule

clean-submodule:
	-$(MAKE) -C deps/Workshop_Computer_VCV clean RACK_DIR=$(abspath $(RACK_DIR))
	rm -rf res/cards res/web res/backyard_rain res/wav res/vcv_bridge.js

install-dev: copy-cards all
	@echo "Installing to $(RACK2_PLUGIN_DIR)/$(PLUGIN_NAME)..."
	@mkdir -p $(RACK2_PLUGIN_DIR)/$(PLUGIN_NAME)
	@cp plugin.dylib $(RACK2_PLUGIN_DIR)/$(PLUGIN_NAME)/
	@cp plugin.json  $(RACK2_PLUGIN_DIR)/$(PLUGIN_NAME)/
	@cp -r res        $(RACK2_PLUGIN_DIR)/$(PLUGIN_NAME)/
	@echo "Done."
