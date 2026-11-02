#pragma once
#include "OrientationSession.h"
#include "WindowTests.h"
class WindowSession
{
    bool resetPending = false;
    std::string preparePending;
    unsigned perturbPending = 0, runPending = 0;
    std::string lastDiagnostic;

  public:
    WindowSolver solver;
    int mode = 0;
    bool initialized = false, autoRelax = false;
    unsigned sweeps = 1;
    void selectMode(int selected, ID3D12Device *device, const Submit &submit, OrientationSession &orientation);
    void initialize(ID3D12Device *device, const Submit &submit, guidegpu::State &state,
                    const std::filesystem::path &shaders);
    void panel(ID3D12Device *device, const Submit &submit, OrientationSession &orientation);
    void record(ID3D12Device *device, ID3D12GraphicsCommandList *list, UploadList &keep,
                OrientationSession &orientation, bool allowAuto = true);
};
void testWindowModeTransitions(WindowSession &window, OrientationSession &orientation,
                               const OrientationTestContext &ctx);
