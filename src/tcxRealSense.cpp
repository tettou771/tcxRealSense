// =============================================================================
// tcxRealSense.cpp - Intel RealSense backend implementation (librealsense 2)
// =============================================================================

#include "tcxRealSense.h"

#include <librealsense2/rs.hpp>

#include <chrono>
#include <cstring>
#include <optional>
#include <sstream>
#include <thread>

using namespace std;
using namespace tc;

namespace tcx::realsense {

using Clock = chrono::steady_clock;
static constexpr auto kPollInterval = chrono::milliseconds(5);
static constexpr auto kErrorBackoff = chrono::milliseconds(100);
static constexpr auto kFrameTimeout = chrono::seconds(2);

struct RealSense::Impl {
    unique_ptr<rs2::context> context;
    rs2::device device{nullptr};
    unique_ptr<rs2::pipeline> pipeline;
    bool started = false;
    bool captureError = false;
    Clock::time_point nextPoll{}, lastFrameAt{};

    // The base rotates buffers. Always carry every stream's latest data into
    // the destination, including streams absent from the current frameset.
    depthcamera::DepthFrame latest;
    optional<uint64_t> depthNumber, colorNumber, infraredNumber;
};

RealSense::RealSense(uint32_t deviceIndex)
    : impl_(make_unique<Impl>()), deviceIndex_(deviceIndex) {}

// Stop the grabber while this backend's members are still alive (#275).
RealSense::~RealSense() { close(); }

static depthcamera::DepthIntrinsics copyIntrinsics(const rs2_intrinsics& in) {
    depthcamera::DepthIntrinsics out;
    out.width = in.width; out.height = in.height;
    out.fx = in.fx; out.fy = in.fy;
    out.cx = in.ppx; out.cy = in.ppy;
    if (in.model == RS2_DISTORTION_BROWN_CONRADY ||
        in.model == RS2_DISTORTION_INVERSE_BROWN_CONRADY) {
        out.k1 = in.coeffs[0]; out.k2 = in.coeffs[1];
        out.p1 = in.coeffs[2]; out.p2 = in.coeffs[3];
        out.k3 = in.coeffs[4];
    }
    return out;
}

static string deviceInfo(const rs2::device& device, rs2_camera_info field) {
    return device.supports(field) ? device.get_info(field) : "";
}

static string supportedProfiles(const rs2::device& device) {
    ostringstream out;
    out << "\nSupported profiles:";
    for (const auto& sensor : device.query_sensors()) {
        for (const auto& profile : sensor.get_stream_profiles()) {
            out << "\n  " << rs2_stream_to_string(profile.stream_type())
                << "[" << profile.stream_index() << "] ";
            if (auto video = profile.as<rs2::video_stream_profile>())
                out << video.width() << "x" << video.height() << " ";
            out << rs2_format_to_string(profile.format()) << " @ " << profile.fps();
        }
    }
    return out.str();
}

bool RealSense::openDevice() {
    string profiles;
    auto fail = [&](const string& reason) {
        logError("tcxRealSense") << "openDevice failed: " << reason << profiles;
        closeDevice();
        return false;
    };
    try {
        impl_->context = make_unique<rs2::context>();
        auto devices = impl_->context->query_devices();
        if (!requestedSerial_.empty()) {
            for (auto device : devices) {
                if (deviceInfo(device, RS2_CAMERA_INFO_SERIAL_NUMBER) == requestedSerial_) {
                    impl_->device = device;
                    break;
                }
            }
            if (!impl_->device) return fail("no device with serial " + requestedSerial_);
        } else {
            if (deviceIndex_ >= devices.size())
                return fail("device index " + to_string(deviceIndex_) + " out of range (" +
                            to_string(devices.size()) + " device(s))");
            impl_->device = devices[deviceIndex_];
        }

        profiles = supportedProfiles(impl_->device);
        if (!isDepthEnabled() && !isColorEnabled() && !isInfraredEnabled())
            return fail("no streams enabled; call enableDepth(), enableColor() or enableInfrared()");
        // Zero dimensions/fps mean automatic selection in the SDK. Reject them
        // here so a requested size is never silently replaced by another mode.
        if (desiredFrameRate_ <= 0 ||
            ((isDepthEnabled() || isInfraredEnabled()) && (depthWidth_ <= 0 || depthHeight_ <= 0)) ||
            (isColorEnabled() && (colorWidth_ <= 0 || colorHeight_ <= 0)))
            return fail("stream dimensions and frame rate must be positive");

        const string serial = deviceInfo(impl_->device, RS2_CAMERA_INFO_SERIAL_NUMBER);
        rs2::config config;
        config.disable_all_streams();
        config.enable_device(serial);
        if (isDepthEnabled())
            config.enable_stream(RS2_STREAM_DEPTH, depthWidth_, depthHeight_, RS2_FORMAT_Z16, desiredFrameRate_);
        if (isColorEnabled())
            config.enable_stream(RS2_STREAM_COLOR, colorWidth_, colorHeight_, RS2_FORMAT_RGBA8, desiredFrameRate_);
        if (isInfraredEnabled())
            config.enable_stream(RS2_STREAM_INFRARED, 1, depthWidth_, depthHeight_, RS2_FORMAT_Y8, desiredFrameRate_);

        impl_->pipeline = make_unique<rs2::pipeline>(*impl_->context);
        if (!config.can_resolve(*impl_->pipeline)) {
            ostringstream requested;
            requested << "unsupported stream combination:";
            if (isDepthEnabled()) requested << " depth " << depthWidth_ << "x" << depthHeight_ << " Z16";
            if (isColorEnabled()) requested << " color " << colorWidth_ << "x" << colorHeight_ << " RGBA8";
            if (isInfraredEnabled()) requested << " left IR " << depthWidth_ << "x" << depthHeight_ << " Y8";
            requested << " @ " << desiredFrameRate_;
            return fail(requested.str());
        }
        auto active = impl_->pipeline->start(config);
        impl_->started = true;

        if (isDepthEnabled()) {
            auto depth = active.get_stream(RS2_STREAM_DEPTH).as<rs2::video_stream_profile>();
            depthIntrinsics_ = copyIntrinsics(depth.get_intrinsics());
            depthScale_ = active.get_device().first<rs2::depth_sensor>().get_depth_scale();
        }
        if (isColorEnabled()) {
            auto color = active.get_stream(RS2_STREAM_COLOR).as<rs2::video_stream_profile>();
            colorIntrinsics_ = copyIntrinsics(color.get_intrinsics());
            if (isDepthEnabled()) {
                const auto ex = active.get_stream(RS2_STREAM_DEPTH).get_extrinsics_to(color);
                // SDK rotation is column-major; Mat4 is row-major. Translation
                // is already meters, the same units as DepthCamera world space.
                Mat4 m;
                m.m[0] = ex.rotation[0]; m.m[1] = ex.rotation[3]; m.m[2] = ex.rotation[6];
                m.m[4] = ex.rotation[1]; m.m[5] = ex.rotation[4]; m.m[6] = ex.rotation[7];
                m.m[8] = ex.rotation[2]; m.m[9] = ex.rotation[5]; m.m[10] = ex.rotation[8];
                m.m[3] = ex.translation[0]; m.m[7] = ex.translation[1]; m.m[11] = ex.translation[2];
                depthToColor_ = m;
            }
        }
        deviceName_ = deviceInfo(impl_->device, RS2_CAMERA_INFO_NAME);
        serial_ = serial;
        firmwareVersion_ = deviceInfo(impl_->device, RS2_CAMERA_INFO_FIRMWARE_VERSION);
        impl_->lastFrameAt = Clock::now();
        logNotice("tcxRealSense") << "opened " << deviceName_ << " (" << serial_ << ")";
        return true;
    } catch (const rs2::error& e) {
        return fail(e.what());
    }
}

void RealSense::closeDevice() {
    try {
        if (impl_->started) impl_->pipeline->stop();
    } catch (const rs2::error& e) {
        logWarning("tcxRealSense") << "stop failed: " << e.what();
    }
    impl_->started = false;
    impl_->pipeline.reset();
    impl_->device = rs2::device(nullptr);
    impl_->context.reset();
    impl_->latest = {};
    impl_->depthNumber.reset();
    impl_->colorNumber.reset();
    impl_->infraredNumber.reset();
    impl_->captureError = false;
    impl_->nextPoll = {};
    impl_->lastFrameAt = {};
    depthIntrinsics_ = {};
    colorIntrinsics_ = {};
    depthToColor_ = {};
    depthScale_ = 0.001f;
    deviceName_.clear(); serial_.clear(); firmwareVersion_.clear();
}

// Respect SDK row strides rather than assuming tightly packed input planes.
static void copyRows(const rs2::video_frame& frame, void* output, size_t rowBytes) {
    const auto* source = static_cast<const uint8_t*>(frame.get_data());
    auto* target = static_cast<uint8_t*>(output);
    for (int y = 0; y < frame.get_height(); ++y)
        memcpy(target + y * rowBytes, source + y * frame.get_stride_in_bytes(), rowBytes);
}

static void ensurePixels(Pixels& pixels, int w, int h, int channels, PixelFormat format) {
    if (!pixels.isAllocated() || pixels.getWidth() != w || pixels.getHeight() != h ||
        pixels.getChannels() != channels || pixels.getFormat() != format)
        pixels.allocate(w, h, channels, format);
}

static void copyPixels(const Pixels& source, Pixels& target) {
    if (!source.isAllocated()) {
        target.clear();
        return;
    }
    ensurePixels(target, source.getWidth(), source.getHeight(), source.getChannels(), source.getFormat());
    memcpy(target.getDataVoid(), source.getDataVoid(), source.getTotalBytes());
}

// Pixels (and therefore DepthFrame) is move-only. Copy into reusable storage;
// buffers must own their planes independently of the worker's latest frame.
static void copyFrame(const depthcamera::DepthFrame& source, depthcamera::DepthFrame& target) {
    target.w = source.w; target.h = source.h;
    target.depthScale = source.depthScale;
    target.depth = source.depth;
    target.world = source.world;
    copyPixels(source.color, target.color);
    target.colorIntrinsics = source.colorIntrinsics;
    target.depthToColor = source.depthToColor;
    target.colorUV = source.colorUV;
    copyPixels(source.ir, target.ir);
    target.intrinsics = source.intrinsics;
    target.timestamp = source.timestamp;
}

depthcamera::StreamFreshness RealSense::captureInto(depthcamera::DepthFrame& dst) {
    depthcamera::StreamFreshness fresh;
    if (!impl_->started) return fresh;

    // Only the worker may sleep. Inline update() uses a deadline and returns
    // immediately during backoff. Recheck the stop flag every 5 ms on close().
    while (isThreaded() && isThreadRunning() && Clock::now() < impl_->nextPoll)
        this_thread::sleep_for(kPollInterval);
    // An empty freshness is not published (the base keeps its front frame),
    // so there is nothing to carry forward on these paths.
    if ((isThreaded() && !isThreadRunning()) || Clock::now() < impl_->nextPoll)
        return fresh;

    try {
        rs2::frameset frames;
        if (!impl_->pipeline->poll_for_frames(&frames)) {
            // poll_for_frames can return false indefinitely after unplugging.
            // Treat a sustained stall like an SDK error, with one log per outage.
            if (!impl_->captureError && Clock::now() - impl_->lastFrameAt >= kFrameTimeout) {
                logError("tcxRealSense") << "no frames for 2 seconds; device disconnected or stream stalled";
                impl_->captureError = true;
            }
            impl_->nextPoll = Clock::now() + (impl_->captureError ? kErrorBackoff : kPollInterval);
            return fresh;
        }

        // Stage a complete frame. An SDK error midway through a copy must not
        // publish partial data or consume a stream's frame number.
        copyFrame(impl_->latest, dst);
        auto depthNumber = impl_->depthNumber;
        auto colorNumber = impl_->colorNumber;
        auto infraredNumber = impl_->infraredNumber;
        dst.intrinsics = depthIntrinsics_;
        dst.colorIntrinsics = colorIntrinsics_;
        dst.depthToColor = depthToColor_;
        dst.depthScale = depthScale_;
        if (auto depth = frames.get_depth_frame()) {
            const auto number = depth.get_frame_number();
            if (!depthNumber || *depthNumber != number) {
                dst.w = depth.get_width(); dst.h = depth.get_height();
                dst.depth.resize(static_cast<size_t>(dst.w) * dst.h);
                copyRows(depth, dst.depth.data(), static_cast<size_t>(dst.w) * sizeof(uint16_t));
                dst.timestamp = depth.get_timestamp() * 0.001;
                dst.world.clear();
                dst.colorUV.clear();
                depthNumber = number;
                fresh.depth = true;
            }
        }
        if (auto color = frames.get_color_frame()) {
            const auto number = color.get_frame_number();
            if (!colorNumber || *colorNumber != number) {
                ensurePixels(dst.color, color.get_width(), color.get_height(), 4, PixelFormat::U8);
                copyRows(color, dst.color.getData(), static_cast<size_t>(color.get_width()) * 4);
                if (!isDepthEnabled()) dst.timestamp = color.get_timestamp() * 0.001;
                colorNumber = number;
                fresh.color = true;
            }
        }
        if (auto ir = frames.get_infrared_frame(1)) {
            const auto number = ir.get_frame_number();
            if (!infraredNumber || *infraredNumber != number) {
                const int w = ir.get_width(), h = ir.get_height();
                ensurePixels(dst.ir, w, h, 1, PixelFormat::F32);
                auto* output = dst.ir.getDataF32();
                const auto* source = static_cast<const uint8_t*>(ir.get_data());
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                        output[static_cast<size_t>(y) * w + x] = source[y * ir.get_stride_in_bytes() + x];
                if (!isDepthEnabled() && !isColorEnabled()) dst.timestamp = ir.get_timestamp() * 0.001;
                infraredNumber = number;
                fresh.infrared = true;
            }
        }
        copyFrame(dst, impl_->latest);
        impl_->depthNumber = depthNumber;
        impl_->colorNumber = colorNumber;
        impl_->infraredNumber = infraredNumber;
        impl_->captureError = false;
        impl_->lastFrameAt = Clock::now();
        impl_->nextPoll = Clock::now() + kPollInterval;
        return fresh;
    } catch (const rs2::error& e) {
        if (!impl_->captureError) logError("tcxRealSense") << "capture failed: " << e.what();
        impl_->captureError = true;
        impl_->nextPoll = Clock::now() + kErrorBackoff;
        return {};
    }
}

} // namespace tcx::realsense
