#pragma once
#include "GuideGpu.h"
#include "WindowMath.h"
struct WindowParameters
{
    uint32_t strands, vertices, segments, bends, participating, iterations, pinX, pinQ;
    float ss, bt, invH2;
    uint32_t firstChunk;
    uint32_t baseSweep, operationLimit, referenceFlags, perturbMode;
};
static_assert(sizeof(WindowParameters) == 64 && offsetof(WindowParameters, ss) == 32 &&
              offsetof(WindowParameters, baseSweep) == 48);
struct WindowFault
{
    uint32_t code, sweep, window, operation, index, strand, pad0, pad1;
};
struct WindowStats
{
    uint32_t attemptedP, executedP, attemptedQ, executedQ, ordinary, fallback, hard, rejected, failures,
        completedSweeps;
    float batchX, batchQ, lastX, lastQ;
};
static_assert(sizeof(WindowFault) == 32 && sizeof(WindowStats) == 56 && offsetof(WindowStats, batchX) == 40);
struct WindowSnapshot
{
    windowmath::State state;
    std::vector<WindowFault> faults;
    std::vector<WindowStats> stats;
};
class WindowSolver
{
    guidegpu::State *state{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline, perturbPipeline;
    guidegpu::Buffer target, faults, stats, batchX, batchQ, sweepX, sweepQ, preparedX, preparedQ;
    windowmath::Settings preparedSettings;unsigned preparedSchedule=0;
    std::vector<Float3> fixedTargets;
    mutable bool prepared = false;
    unsigned requestedSweeps{};
    bool hasRun = false;
    uint64_t generation = 0;
    void bind(ID3D12GraphicsCommandList *list);
    WindowParameters parameters(unsigned iterations, bool first, unsigned base, unsigned flags = 0,
                                unsigned limit = 0) const;
    void dispatch(ID3D12GraphicsCommandList *list, unsigned sweeps, unsigned flags, unsigned limit, unsigned chunk,
                  unsigned base);

  public:
    windowmath::Settings settings;
    unsigned schedule=0;bool uncachedValidation=false;
    std::string targetMode;
    static constexpr unsigned ChunkSweeps = 8;
    void validateSettings() const;
    void runDynamic(ID3D12GraphicsCommandList *list, unsigned sweeps, bool diagnosticSnapshots = false,unsigned baseSweep=0);
    guidegpu::Buffer &batchPositionBuffer(){return batchX;}
    guidegpu::Buffer &batchQuaternionBuffer()
    {
        return batchQ;
    }
    guidegpu::Buffer &targetBuffer()
    {
        return target;
    }
    guidegpu::Buffer &faultBuffer()
    {
        return faults;
    }
    guidegpu::Buffer &statsBuffer()
    {
        return stats;
    }
    void initialize(ID3D12Device *device, ID3D12GraphicsCommandList *list, guidegpu::State &shared,
                    const std::filesystem::path &shaders, UploadList &keep);
    void invalidate()
    {
        prepared = false;
    }
    bool ready() const;
    unsigned participating() const;
    void prepare(ID3D12Device *device, ID3D12GraphicsCommandList *list, const std::vector<Float3> &targets,
                 const std::string &mode, UploadList &keep);
    void resetCurrent(ID3D12Device *device, ID3D12GraphicsCommandList *list, UploadList &keep);
    void perturb(ID3D12GraphicsCommandList *list, unsigned mode);
    void run(ID3D12GraphicsCommandList *list, unsigned sweeps, unsigned referenceFlags = 0, unsigned operationLimit = 0,
             unsigned chunkSize = ChunkSweeps);
    WindowSnapshot read(ID3D12Device *device, const Submit &submit);
    std::string diagnostics(ID3D12Device *device, const Submit &submit, const WindowSnapshot &snapshot);
    const GuideCpuState &cpu() const
    {
        return *state->cpu;
    }
};
