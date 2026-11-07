# Frame Perfect Assist

An Android 64-bit Geode mod for Geometry Dash 2.2081.

## Current prototype

- Configurable correction window: 2–10 physics frames (default: 5).
- The Auto-click setting currently enables diagnostics only; it does not send input.
- Cube-stage scanner: searches the level object list for the nearest object classified as a hazard.
- Measures horizontal movement using the game's update delta and smooths the measured speed to reduce noisy estimates.
- Estimates arrival in physics frames using the 240 Hz physics-step rate. This is a speed-based estimate, not yet a validated prediction of exact collision or jump timing.
- Logs the first detection, meaningful estimate changes, and when the estimate enters the configured correction window.
- Android 64-bit package builds through GitHub Actions.

**Not implemented yet:** validated jump physics, safe jump-frame selection, automatic input through `queueButton`, and support for the other vehicles. A successful build confirms compilation, not correct behavior in a live level.

## Development order

1. Validate hazard detection and arrival estimates in cube gameplay.
2. Compare the estimate with real obstacle contact across different speed portals and level sections.
3. Calculate and test cube jump timing using verified game physics.
4. Add automatic jump input only after timing is validated.
5. Expand and test vehicle modes one by one.
