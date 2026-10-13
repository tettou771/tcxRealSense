# Licenses

## tcxRealSense

MIT License — © tettou771

## Dependencies (not bundled in this repository)

- **Intel RealSense SDK 2.0 (librealsense)** — Apache License 2.0, © Intel
  Corporation. Must be installed separately.
  https://github.com/IntelRealSense/librealsense
  The SDK source, headers, import libraries, tools and installer are not
  redistributed in this addon. Only its runtime **realsense2.dll** is bundled
  next to a Windows app executable at build time, from the user's SDK install.
  The unverified Linux CMake path similarly bundles the runtime shared library.
  Distributors of an app containing that runtime must retain the SDK's applicable
  license and notices; see the installed SDK and upstream `LICENSE`/`NOTICE`.
- **tcxDepthCamera** — MIT, the TrussC depth-camera interface this addon implements.
