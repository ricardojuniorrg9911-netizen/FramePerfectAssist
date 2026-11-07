name: Build Geode mod

on:
  workflow_dispatch:
  push:
    branches: [ main ]

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7

      - name: Build Android 64-bit mod
        uses: geode-sdk/build-geode-mod@main
        with:
          target: Android64
