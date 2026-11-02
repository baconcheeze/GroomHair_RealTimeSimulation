#pragma once
#include "GuideGpu.h"
struct OrientationParameters {
    uint32_t strands,vertices,segments,bends,participating,iterations,pinRoot,firstChunk;
    float baseSS,baseBT;uint32_t padding[2]{};
};
static_assert(sizeof(OrientationParameters)==48&&offsetof(OrientationParameters,baseSS)==32&&offsetof(OrientationParameters,firstChunk)==28);
struct SolveStats {uint32_t regular,fallback,hard,rejected,failures;float lastSweep;};
static_assert(sizeof(SolveStats)==24&&offsetof(SolveStats,lastSweep)==20);
struct OrientationSettings {unsigned participating=1,sweeps=32;bool pinRoot=true,autoSolve=false,showFrames=false;unsigned frameLimit=1,frameStride=4;float axisLength=.004f,baseSS=1,baseBT=1e-5f;};
class OrientationSolver {
    guidegpu::State* state{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
public:
    OrientationSettings settings;
    guidegpu::Buffer errors,stats;
    bool hasRun=false;
    void initialize(ID3D12Device* device,ID3D12GraphicsCommandList* list,guidegpu::State& state_,const std::filesystem::path& shaders,UploadList& keep);
    void resetStatus(ID3D12Device* device,ID3D12GraphicsCommandList* list,UploadList& keep);
    void run(ID3D12GraphicsCommandList* list,unsigned sweeps);
    unsigned participating() const;
};
