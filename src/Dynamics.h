#pragma once
#include "OrientationTests.h"
#include "WindowSolver.h"
#include <array>
struct DynamicsStatus
{
    uint32_t code, substep, strand, operation, index, completed, ordinary, fallback, hard, failures, beginCount,
        endCount;
};
static_assert(sizeof(DynamicsStatus) == 48);
struct DynamicsParameters
{
    uint32_t strands, participating, pinX, pinQ;
    float h, gx, gy, gz;
    uint32_t substep, initializer, pad1, pad2;
    float kx, ky, kz, pad3;
};
static_assert(sizeof(DynamicsParameters) == 64 && offsetof(DynamicsParameters, h) == 16 &&
              offsetof(DynamicsParameters, kx) == 48);
struct FixedStepClock
{
    double remainder = 0, discarded = 0;
    unsigned advance(double elapsed, double h, bool running)
    {
        if (!(std::isfinite(elapsed) && elapsed >= 0 && std::isfinite(h) && h > 0))
            throw std::runtime_error("Invalid clock input");
        if (!running)
        {
            remainder = 0;
            return 0;
        }
        double total = remainder + elapsed, cap = 8 * h;
        if (total > cap)
        {
            discarded += total - cap;
            total = cap;
        }
        unsigned n = std::min(8u, unsigned(std::floor((total + 1e-12 * h) / h)));
        remainder = std::max(0., total - n * h);
        return n;
    }
};
struct DynamicsTiming
{
    Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    uint64_t frequency = 0;
    void initialize(ID3D12Device *, ID3D12CommandQueue *);
    std::array<double, 3> milliseconds() const; // Caller must wait for query resolve fence.
};
struct DynamicsSnapshot
{
    WindowSnapshot guide;
    std::vector<Float3> velocity, previous, target;
    std::vector<DynamicsStatus> status;
};
class DynamicsSolver
{
    guidegpu::State *state{};
    WindowSolver *window{};
    guidegpu::Buffer previous, status, acceleration;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> beginPipeline, endPipeline, kickPipeline;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> statusReadback;
    std::array<bool, 2> pollPending{};
    windowmath::Settings preparedSettings;
    Float3 preparedGravity{};
    unsigned preparedSweeps = 8;
    unsigned preparedInitializer=0,preparedSchedule=0;
    mutable bool configured = false;
    bool inside = false;
    void bind(ID3D12GraphicsCommandList *list, Float3 kick = {});
    void barriers(ID3D12GraphicsCommandList *list);

  public:
    windowmath::Settings settings;
    Float3 gravity{0, -9.81f, 0};
    unsigned sweeps = 8, queuedSteps = 0, observedCompleted = 0;
    uint64_t resetGeneration=0;
    // 0 FullPrediction (default); 1 InertialOnly, 2 raw history (benchmark only; not accepted).
    // 3 same-energy Full/Inertial guard (research candidate; reference accuracy gate unmet).
    unsigned initializer=0,windowSchedule=0;
    guidegpu::Buffer& accelerationBuffer(){return acceleration;}
    guidegpu::Buffer& validationStatusBuffer(){return status;} // Explicit frozen-state tests only.
    bool failed = false, diagnosticSnapshots = false, metricsLastStep = false, velocityChangedSinceEnd = false;
    std::string failure;
    void initialize(ID3D12Device *, ID3D12GraphicsCommandList *, guidegpu::State &, WindowSolver &,
                    const std::filesystem::path &, UploadList &);
    bool ready() const;
    void invalidate()
    {
        configured = false;
    }
    void reset(ID3D12Device *, ID3D12GraphicsCommandList *, UploadList &);
    void begin(ID3D12GraphicsCommandList *);
    void solve(ID3D12GraphicsCommandList *, bool snapshots = false);
    void end(ID3D12GraphicsCommandList *);
    void step(ID3D12GraphicsCommandList *, unsigned count, DynamicsTiming *timer = nullptr);
    void kick(ID3D12GraphicsCommandList *, Float3 amplitude);
    void recordStatusPoll(ID3D12GraphicsCommandList *, unsigned slot);
    void observe(unsigned slot); // Only after that frame-slot fence has completed.
    DynamicsSnapshot read(ID3D12Device *, const Submit &);
    std::string diagnostics(ID3D12Device *, const Submit &, const DynamicsSnapshot &);
    double simulationTime() const
    {
        return double(queuedSteps) * preparedSettings.h;
    }
    const GuideCpuState &cpu() const
    {
        return *state->cpu;
    }
};
void testDynamicsLocal(const OrientationTestContext &);
void testDynamicsGroom(guidegpu::State &, WindowSolver &, DynamicsSolver &, const OrientationTestContext &,
                       ID3D12CommandQueue *,Float3 gravitySolver);
