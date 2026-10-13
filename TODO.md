# tcxRealSense — Known issues & roadmap

Status: **Windows-first v1, not yet hardware-verified.** Backend and example
pass C++20 syntax checks against the real librealsense v2.59.1 headers. No SDK
runtime or camera was available for linking/running. Linux CMake is written but
unverified with a real SDK; fake-SDK configure checks do not establish support.

## Windows D435 verification (before merge)

1. Build `example-basic` with the installed SDK, without `-Drealsense2_DIR`.
   Check DLL bundling, depth, color, left IR, colored point cloud and FPS.
2. Press R for repeated `setup()`; press X for `close()` then `setup()`.
3. Unplug while running: no crash, one Error and low CPU. Replug, then X to
   recover. Check that a frame-less tick preserves the displayed frame.
4. Exit without an explicit `close()`; confirm the process exits cleanly and
   `rs-enumerate-devices` can open the device afterwards.
5. Start a second instance / `rs-enumerate-devices` while the app runs: streaming
   continues or fails clearly, without a crash.
6. Also check serial selection, disabled/single streams, unsupported size/fps
   diagnostics, inline polling, calibration/color alignment and meters.

## Linux and other devices

7. Build and link with a real distro/Intel SDK, then verify runtime bundling,
   USB/udev permissions and the same lifecycle checklist. A Pi 5 run is later.
   Keep Linux out of `addon.json` until verified.
8. Check other D4xx models and two-camera index/serial selection.

## Calibration

9. The base stores Brown-Conrady coefficients without a distortion-model tag
   and uses pinhole color projection. Inverse Brown-Conrady coefficients are
   preserved, but exact inverse-model deprojection / distorted color mapping
   need a coordinated base change or a future SDK world/UV path.

## Out of scope for v1

10. Registry listing is a separate PR after v1. Root idea 1 / #415 lifecycle
    changes must update this addon together with tcxOrbbec / tcxAzureKinect.
    macOS, IMU, stereo-pair access and multi-camera synchronization are later work.
