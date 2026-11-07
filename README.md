# Frame Perfect Assist

Starter source project for an Android 64-bit Geode mod for Geometry Dash.

## Current status

- Includes a configurable setting named `correction_frames`.
- Minimum: 2 frames.
- Maximum: 10 frames.
- Default: 5 frames.
- Logs the selected value when the mod loads.

**Important:** this is only a starter template. It does not yet detect frame-perfect opportunities, intercept taps, or reschedule input. It is not an installable `.geode` mod until it is built and tested against the exact installed Geometry Dash and Geode versions.

## Build

The GitHub Actions workflow is a starting point for an Android 64-bit build. Confirm that its SDK/action versions match the installed Geode SDK before treating a successful build as proof of compatibility.
