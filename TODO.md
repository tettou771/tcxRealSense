# tcxRealSense — Known issues & roadmap

Status: **Windows v1, hardware-verified** on a D435 (FW 5.17.0.10, USB 3.2)
with RealSense SDK v2.59.1 and VS 2026. Linux CMake is written but unverified
with a real SDK; fake-SDK configure checks do not establish support.

## Windows D435 verification (done)

All passed on the D435 above; details are in the v1 PR.

1. `example-basic` builds with the installed SDK (no `-DREALSENSE_SDK_DIR`),
   bundles realsense2.dll, and shows depth, color, left IR, a colored point
   cloud and 30 fps.
2. R (repeated `setup()`) and X (`close()` then `setup()`) keep streaming.
3. Unplug while running: no crash, one Error ("no frames for 2 seconds"), CPU
   drops to a few percent, the last frame stays on screen. Replug + X recovers.
4. Exit without an explicit `close()`: clean exit (code 0), and
   `rs-enumerate-devices` / `rs-save-to-disk` open the device afterwards.
5. A second instance fails with one Error (`Device or resource busy`) while the
   first keeps streaming; `rs-enumerate-devices` works alongside the app.
6. Serial selection, color-only and IR-only (640x480 @ 60) setups, inline
   polling, unsupported size / unknown serial / bad index diagnostics, meters
   (depth scale 0.001) and the depth->color mapping were checked with a
   throwaway app.

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
