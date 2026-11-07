# Frame Perfect Assist

An Android 64-bit Geode mod for Geometry Dash 2.2081.

## Current prototype

- Configurable correction window: 2–10 frames (default: 5).
- Auto-click setting can be enabled or disabled by the player.
- Initial cube-stage scanner: when Auto-click is enabled, it searches the level's object list for the nearest hazard hitbox ahead of the cube and reports the detected object in the Geode log.
- Android 64-bit package builds through GitHub Actions.

**Not implemented yet:** choosing a safe jump frame, automatically sending a jump with `queueButton`, and support for the other vehicles. The current scanner is diagnostic only; enabling Auto-click does not make the cube jump yet. A successful build confirms compilation, not correct behavior in a live level.

## Development order

1. Validate hazard detection and distance on cube gameplay.
2. Calculate and test jump timing against cube physics.
3. Add automatic jump input only after timing is validated.
4. Expand and test vehicle modes one by one.
