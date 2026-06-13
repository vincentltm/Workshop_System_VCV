# Workshop System VCV Rack Plugin

This is the VCV Rack plugin for the analog section of the **Music Thing Modular Workshop System**.

## Two-Repo Architecture

This plugin relies on the standalone **Workshop Computer VCV** plugin as a submodule to provide the computer engine and shared widgets:

1. **[Workshop_Computer_VCV](https://github.com/vincentltm/Workshop_Computer_VCV)**: Standalone computer plugin (slug: `MTMWorkshopComputer`). Source of truth for all computer card hosting, DSP processing, right-click menus, and web server functions.
2. **Workshop_System_VCV** (This repository): Analog section plugin (slug: `MTMWorkshopSystem`). Embeds the computer plugin via a git submodule to keep the embedded computer functionally and visually identical to the standalone version.

## Build Requirements

1. **VCV Rack SDK**: Extracted into `dep/Rack-SDK`.
2. **Workshop Computer submodule**: Must be checked out and initialized.

## Building the Plugin

To initialize the submodule and compile the plugin:

```bash
# Initialize and fetch the submodule
git submodule update --init --recursive

# Build the plugin (this will automatically copy compiled card dylibs and web resources)
make -j8
```

## Developer Direct Installation

To install directly to your local VCV Rack 2 plugins directory:

```bash
make install-dev
```
