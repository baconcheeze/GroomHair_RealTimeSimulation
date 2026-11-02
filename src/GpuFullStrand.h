#pragma once
#include "NativeReferenceDemo.h"
#include "GuideGpu.h"
#include "DebugScene.h"
class GpuFullStrand {
 struct Impl; std::unique_ptr<Impl> p;
public:
 unsigned refinements=1;
 GpuFullStrand(); ~GpuFullStrand();
 void initialize(ID3D12Device*, const Submit&, const GuideCpuState&, const std::filesystem::path&, Float3, unsigned, UINT64);
 bool initialized()const; void reset(unsigned); void run(bool); bool running()const;
 void steps(unsigned); void kick(Float3); uint64_t generation()const;
 std::shared_ptr<const NativeReferenceSnapshot> snapshot()const;
 std::shared_ptr<const NativeReferenceSnapshot> readCompleted();
 void audit(const std::filesystem::path&);
 void observe(unsigned); void record(ID3D12GraphicsCommandList*,unsigned,bool,bool);
 void draw(ID3D12GraphicsCommandList*,ID3D12RootSignature*,ID3D12PipelineState*,const DirectX::XMFLOAT4X4&,unsigned);
};
