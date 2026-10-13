#pragma once

#include <TrussC.h>
#include <tcxDepthCamera.h>
#include <tcxRealSense.h>
using namespace std;
using namespace tc;
using namespace tcx::realsense;

// Point-cloud viewer driven by a RealSense camera through the tcxDepthCamera
// interface: update() -> isFrameNew() -> toMesh(). Requires the hardware + the
// RealSense SDK 2.0. If no device is found, the app still runs and shows a message
// rather than crashing.
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void rebuild();

    shared_ptr<RealSense> camera;
    bool deviceOk = false;
    EasyCam view;
    Mesh cloud;
    bool colored = true;    // RealSense has color; show it by default
    int step = 2;           // decimate a bit

    float    fps_ = 0.0f;     // smoothed camera frame rate (from device timestamps)
    double   lastDevTs_ = 0;  // previous device timestamp (seconds)
};
