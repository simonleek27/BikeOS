# Display simulators

The authoritative simulator is now `build-host/bikeos_simulator`: a native SDL
application linked directly to the shared C++ navigation and framebuffer code.
Build and controls are documented in the repository root README.

The files in this directory are the older browser UI concept. They remain as a
design reference but do not share code with the ESP firmware and are not used
for navigation acceptance.
