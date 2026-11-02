#pragma once
#include "OrientationTests.h"
class OrientationSession {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> frameRoot;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> framePso;
    ID3D12Resource* segmentStarts{};
    std::string pending,lastDiagnostics,actionNotice;
    unsigned pendingSweeps{};
    void createFrames(ID3D12Device* device,const std::filesystem::path& shaders);
public:
    void cancelQueued(){pending.clear();pendingSweeps=0;solver.settings.autoSolve=false;lastDiagnostics.clear();actionNotice.clear();}
    GuideCpuState cpu;guidegpu::State state;OrientationSolver solver;bool ready=false;
    void initialize(ID3D12Device* device,const Submit& submit,const Strands& guide,ID3D12Resource* current,ID3D12Resource* rest,ID3D12Resource* connectivity,const std::filesystem::path& shaders);
    void startup(ID3D12Device* device,const Submit& submit,const std::string& mode,unsigned sweeps,bool resetAfter);
    void panel(ID3D12Device* device,const Submit& submit);
    void record(ID3D12Device* device,ID3D12GraphicsCommandList* list,UploadList& keep,bool allowAuto=true);
    void drawFrames(ID3D12GraphicsCommandList* list,const DirectX::XMFLOAT4X4& matrix);
};
