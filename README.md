# tcxRealSense

Intel RealSense depth-camera support for [TrussC](https://github.com/TrussC-org/TrussC),
implementing the [`tcxDepthCamera`](https://github.com/TrussC-org/TrussC/tree/main/addons/tcxDepthCamera)
interface. First target: **D435**. Other D4xx stereo cameras should work but are
untested.

> **Status: Windows v1, hardware-verified** on a D435 (firmware 5.17.0.10,
> USB 3.2) with RealSense SDK v2.59.1: `example-basic` streams depth, color, IR
> and a colored point cloud at 30 fps, and the lifecycle checklist (re-setup,
> unplug / replug, exit through the destructor, a second process) passes. Linux
> CMake wiring exists but is unverified; Linux is not advertised in
> `addon.json`. See `TODO.md`.

## Requirements

- TrussC with the `tcxDepthCamera` addon and its #275 lifecycle fixes.
- Intel RealSense SDK 2.0 (librealsense), installed separately. The reference
  Windows installer is **v2.59.1**, x64.
- A D435 connected over USB 3 for the default stream combination.

## Installing the RealSense SDK

### Windows

Install SDK 2.0 from the
[librealsense releases](https://github.com/realsenseai/librealsense/releases)
(the project moved from IntelRealSense/librealsense; the v2.59.1 installer is
`RealSense.SDK-WIN10-2.59.1.11053.exe`).
The Windows installer ships **no CMake package config**. This addon searches:

1. `-DREALSENSE_SDK_DIR="<sdk>"`, if supplied (always wins), else a
   `REALSENSE_SDK_DIR` environment variable.
2. `%USERPROFILE%\Documents\RealSense SDK 2.0` (the v2.59 installer's per-user
   default).
3. `C:\Program Files (x86)\Intel RealSense SDK 2.0`.

The selected SDK must contain `include/librealsense2/rs.hpp`,
`lib/x64/realsense2.lib`, and `bin/x64/realsense2.dll`. A normal installation
needs no `-Drealsense2_DIR` or PATH change. CMake links the import library and
bundles **only `realsense2.dll`** beside the app executable at build time through
`tc_addon_bundle_file`. No `rsutils.lib` is required.

### Linux (unverified)

Install `librealsense2-dev` and the SDK's USB/udev rules using Intel's
[Linux package instructions](https://github.com/IntelRealSense/librealsense/blob/v2.59.1/doc/distribution_linux.md)
or a distribution package. CMake first uses `find_package(realsense2 CONFIG)`
and `realsense2::realsense2`. If no target is available it searches for
`librealsense2/rs.hpp` and `librealsense2.so` in the usual system paths;
`REALSENSE_SDK_DIR` supplies include/lib hints for a custom install. The shared
library is registered for runtime bundling just like the Windows DLL.

Only configure checks with a fake SDK and header syntax checks were possible
here. A real Linux link, runtime-library deployment, USB permissions and camera
capture still need verification. macOS is out of scope for v1.

## Usage

Place this repository at `TrussC/addons/tcxRealSense`. Add `tcxRealSense` and
`tcxDepthCamera` to the application's `addons.make` (as in `example-basic`).
Generate/build the example with the usual TrussC project tooling.

```cpp
#include <tcxRealSense.h>

// Keep the camera alive across setup/update/draw.
auto camera = std::make_shared<tcx::realsense::RealSense>(); // device index 0
// camera->setSerial("050122070402"); // optional; takes precedence over index
camera->setThreaded(true);
camera->enableDepth();
camera->enableColor();
camera->enableInfrared();
if (!camera->setup()) return; // one Error describes the failure

// In the application's update():
camera->update();
if (camera->isFrameNew()) {
    tc::Mesh cloud = camera->toMesh({.colors = true, .step = 2});
    // Keep cloud for draw().
}
```

Settings are applied on the next `setup()`; call them on the main thread before
opening. `setDepthSize(w, h)` also sets the left IR size; `setColorSize(w, h)`
sets native color resolution; `setDesiredFrameRate(fps)` applies to all enabled
streams. Defaults are **848×480 depth Z16**, **1280×720 color RGBA8**, and
**848×480 left IR Y8 (stream index 1)**, all **30 fps**. Stream enables are all
off by default. An unsupported combination fails `setup()` with one Error
listing the device's modes for the requested streams (resolution and frame
rates, e.g. `848x480 @ 90/60/30/15/6`), without substituting another mode.
`setSerial("")` restores index selection.

`getDeviceName()`, `getSerial()` and `getFirmwareVersion()` report SDK device
information after setup; closing clears it. `getSensorType()` returns `Stereo`.
After a failed `setup()`, `getLastError()` holds the reason (no device, an
unknown serial, `Device or resource busy` when another process has the camera,
an unsupported combination); a successful `setup()` clears it.

## Example controls

`example-basic` shows a colored point cloud, native color/depth/IR thumbnails,
serial/firmware, capture timestamp and smoothed camera FPS.

- **C:** colored/plain cloud; **O:** orthographic/perspective view.
- **1–4:** point-cloud decimation; **drag:** orbit.
- **R:** call `setup()` again (also retries a failed open).
- **X:** `close()` then `setup()`; use after reconnecting a camera.
- Close the window to exercise cleanup through the camera destructor.

## Notes

- **Units:** raw depth is uint16, with meters per unit from the depth sensor's
  `get_depth_scale()`. World coordinates are meters, +X right, +Y down, +Z away.
  SDK timestamps are converted from milliseconds to seconds. Depth supplies
  the timestamp when enabled; color or IR supplies it in single-stream use.
- **Color** stays RGBA at its native resolution. The SDK supplies RGBA8; the
  addon does no color conversion or alignment. The base maps depth to color
  on demand using cached intrinsics and the depth→color extrinsic. SDK rotation
  is column-major and is transposed into TrussC's row-major `Mat4`; translation
  is already meters.
- **Intrinsics:** `ppx/ppy` map to `cx/cy`; Brown-Conrady and inverse
  Brown-Conrady coefficients `[k1,k2,p1,p2,k3]` are preserved. Other distortion
  models leave coefficients zero. The current base has no distortion-model
  tag: it deprojects as Brown-Conrady and projects color as a pinhole. Exact
  inverse-model deprojection and distorted color projection remain base/API
  limitations; D435 depth is usually undistorted. No SDK world/UV map is filled.
- **IR:** the left imager's Y8 samples become single-channel F32 brightness
  values (0–255), matching the base's infrared preview converter.
- **Lifecycle:** `setup()` closes first; the destructor calls `close()` while
  the backend is alive. A tick without a new stream keeps the front frame.
  Per-stream frame numbers determine freshness, and unchanged streams carry
  forward through the base's rotating buffers.
- **Capture:** use `setThreaded(true)` for background grabbing. Polling never
  waits for a frame on the main thread. An SDK capture error logs once per
  outage and retries about every 100 ms until a good frame or close. A two-second
  stall uses the same policy because unplugging can return no frames without
  throwing. Worker sleeps check for close every 5 ms; inline capture uses a
  deadline instead of sleeping. Device open/stop are synchronous SDK calls.

## License

MIT. See `LICENSES.md`. The RealSense SDK (Apache-2.0) must be installed
separately; its runtime DLL is copied into Windows app builds.
