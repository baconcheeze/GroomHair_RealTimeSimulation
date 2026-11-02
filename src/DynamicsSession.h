#pragma once
#include "Dynamics.h"
#include <chrono>
class DynamicsSession
{
    std::chrono::steady_clock::time_point baseline = std::chrono::steady_clock::now();
    bool suspended = false, resetPending = false, kickPending = false;
    unsigned pendingSteps = 0;
    windowmath::Settings savedFixedSettings;
    std::string message;
    WindowSolver *window{};
    DirectX::XMFLOAT4X4 worldToHairRotation{};

  public:
    DynamicsSolver solver;
    FixedStepClock clock;
    bool initialized = false, running = false, deterministic = false;
    Float3 kickAmplitude{0, .02f, 0};
    Float3 worldGravity{0,-9.81f,0};
    unsigned stepCount = 10;
    void initialize(ID3D12Device *, const Submit &, guidegpu::State &, WindowSolver &, const std::filesystem::path &,const DirectX::XMFLOAT4X4&);
    void setWorldGravity(Float3 value){worldGravity=value;solver.gravity=worldVectorToHair(value,worldToHairRotation);}
    void modeChanged(int previous, int next, ID3D12Device *, const Submit &);
    void panel(ID3D12Device *, const Submit &);
    void record(ID3D12Device *, ID3D12GraphicsCommandList *, UploadList &, unsigned slot);
    void observe(unsigned slot)
    {
        solver.observe(slot);
        if (solver.failed)
        {
            running = false;
            pendingSteps = 0;
            message = solver.failure;
        }
    }
    void suspend()
    {
        if (!suspended)
        {
            baseline = std::chrono::steady_clock::now();
            suspended = true;
            clock.remainder = 0;
        }
    }
};
class WindowSession;
class OrientationSession;
void testDynamicsModeTransitions(DynamicsSession &, WindowSession &, OrientationSession &,
                                 const OrientationTestContext &);
