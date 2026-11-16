# BikeOS display simulator

This browser preview renders the BikeOS v1 display at the planned LCD's native
400 × 240 resolution with a one-bit palette. It is a desktop framebuffer preview,
not evidence that the physical LCD has been integrated.

Run from the repository root:

```sh
python3 -m http.server 4173 --directory simulator
```

Then open <http://127.0.0.1:4173>.

The controls cover all required v1 display states and a deterministic Amsterdam
ride sequence. No external packages or network connection are required.
