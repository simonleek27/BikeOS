# BikeOS

Phone-free bike navigation screen for a non-electric Swapfiets bike. Pick a destination on your phone, put the phone in your pocket, ride by the screen on the handlebar.

## Current prototype status

Hardware bring-up is active on the `codex/hardware-bringup` development branch.
The connected prototype is verified as an ESP32-S3 with 16 MB flash, 8 MB
PSRAM, and native USB Serial/JTAG. The first ESP-IDF firmware implements startup
diagnostics and a periodic heartbeat. It has been built, flashed, and verified
on the physical board over USB serial.

The LCD is physically available but intentionally not wired until its exact
model, breakout revision, connector labels, and the carrier-board pinout are
verified.

- [docs/architecture.md](docs/architecture.md): agreed v1 design (phone routes, ESP has its own GPS and follows the route, reroute on request).
- [docs/parts-amsterdam.md](docs/parts-amsterdam.md): parts sourcing and 3D printing options around Amsterdam.
- [docs/hardware-setup.md](docs/hardware-setup.md): verified board facts and hardware setup status.
- [docs/progress.md](docs/progress.md): implemented, tested, blocked, and remaining work.
- [firmware/README.md](firmware/README.md): ESP-IDF build, flash, and monitor workflow.
- [brainstorming.md](brainstorming.md): original concept and research backlog. Section 2 is superseded by the architecture doc.
