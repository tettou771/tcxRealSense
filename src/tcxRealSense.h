#pragma once

// =============================================================================
// tcxRealSense.h - Intel RealSense backend for tcxDepthCamera
// =============================================================================
//
// Drives a D435 (D4xx stereo family) through the unified DepthCamera interface.
// Fills the canonical frame with depth, native RGBA color, left IR and cached
// calibration. The base provides deprojection, meshing, previews and threading.
// RealSense SDK (rs2::) handles stay behind a pImpl; this header is SDK-free.
//
// =============================================================================

#include <tcxDepthCamera.h>
#include <cstdint>
#include <memory>
#include <string>

namespace tcx::realsense {

class RealSense : public depthcamera::DepthCamera {
public:
    // deviceIndex selects among connected RealSense devices (0 = first).
    explicit RealSense(uint32_t deviceIndex = 0);
    ~RealSense() override;

    // Call settings before setup(). A nonempty serial takes precedence over
    // deviceIndex; an empty serial restores index selection.
    void setSerial(const std::string& serial) { requestedSerial_ = serial; }
    void setDepthSize(int w, int h) { depthWidth_ = w; depthHeight_ = h; }
    void setColorSize(int w, int h) { colorWidth_ = w; colorHeight_ = h; }
    void setDesiredFrameRate(int fps) { desiredFrameRate_ = fps; }

    // SDK device information, empty until a successful setup().
    const std::string& getDeviceName() const { return deviceName_; }
    const std::string& getSerial() const { return serial_; }
    const std::string& getFirmwareVersion() const { return firmwareVersion_; }

    // Why the last setup() failed (e.g. "Device or resource busy" when another
    // process holds the camera); empty after a successful setup().
    const std::string& getLastError() const { return lastError_; }

    depthcamera::DepthSensorType getSensorType() const override {
        return depthcamera::DepthSensorType::Stereo;
    }

protected:
    bool openDevice() override;
    void closeDevice() override;
    depthcamera::StreamFreshness captureInto(depthcamera::DepthFrame& dst) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    uint32_t deviceIndex_;
    std::string requestedSerial_;
    std::string deviceName_, serial_, firmwareVersion_;
    std::string lastError_;
    int depthWidth_ = 848, depthHeight_ = 480;
    int colorWidth_ = 1280, colorHeight_ = 720;
    int desiredFrameRate_ = 30;

    depthcamera::DepthIntrinsics depthIntrinsics_{};
    depthcamera::DepthIntrinsics colorIntrinsics_{};
    tc::Mat4 depthToColor_{};
    float depthScale_ = 0.001f;
};

} // namespace tcx::realsense
