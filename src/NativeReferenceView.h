#pragma once
#include "DebugScene.h"
#include "GpuFullStrand.h"
#include "NativeReferenceDemo.h"
#include "OrbitCamera.h"
#include <d3d12.h>
#include <wrl/client.h>
class NativeReferenceView {
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> buffers;
  std::array<size_t, 2> capacity{};

public:
  NativeReferenceDemo simulation;
  GpuFullStrand gpu;
  bool gpuBackend=false;
  ID3D12Device* gpuDevice{}; Submit gpuSubmit; UINT64 gpuFrequency{};
  const GuideCpuState* gpuCpu{};std::filesystem::path gpuDir;
  void setBackend(bool); void reset(unsigned);void run(bool);bool running()const;void steps(unsigned);void kick(Float3);uint64_t generation()const;
  void observe(unsigned f){if(gpuBackend)gpu.observe(f);}
  void record(ID3D12GraphicsCommandList*l,unsigned f){if(gpuBackend)gpu.record(l,f,ghost,traces);}
  void captureState(){if(gpuBackend)current=gpu.readCompleted();}
  std::shared_ptr<const NativeReferenceSnapshot> current;
  bool traces = true, ghost = true, visible = true, focusPending = true,
       tipFocus = false;
  unsigned count = 3, stepN = 4;
  Float3 kickWorld{0, .05f, 0};
  DirectX::XMFLOAT4X4 worldToHair{}, basis{};
  void initialize(const GuideCpuState &, const std::filesystem::path &,
                  const RestData &, unsigned, bool);
  void refresh(OrbitCamera &);
  void focus(OrbitCamera &, bool tip);
  void panel(OrbitCamera &);
  void draw(ID3D12Device *, ID3D12GraphicsCommandList *, ID3D12RootSignature *,
            ID3D12PipelineState *, const DirectX::XMFLOAT4X4 &, unsigned);
};
