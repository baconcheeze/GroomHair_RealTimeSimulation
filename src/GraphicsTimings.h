#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <filesystem>
#include <fstream>

// Optional timestamp-only instrumentation. Never waits; caller owns fences.
class GraphicsTimings {
public:
  enum Stage { Total, Ground, Head, Jacket, RenderHair, Guides, Colliders, Bones,
               Resolve, UI, StageCount };
  void Init(ID3D12Device*,ID3D12CommandQueue*,const std::filesystem::path&);
  void Collect(UINT64 completedFence);
  void BeginFrame(ID3D12GraphicsCommandList*,unsigned slot,unsigned frame,unsigned step);
  void Begin(ID3D12GraphicsCommandList*,Stage);
  void End(ID3D12GraphicsCommandList*,Stage);
  void EndFrame(ID3D12GraphicsCommandList*,UINT64 completionFence);
  const std::array<double,StageCount>& LastMilliseconds() const { return last; }
private:
  static constexpr unsigned Queries=2*StageCount;
  Microsoft::WRL::ComPtr<ID3D12QueryHeap> heap;
  Microsoft::WRL::ComPtr<ID3D12Resource> readback;
  std::ofstream csv;
  UINT64 frequency=0;
  unsigned currentSlot=0;
  struct Slot {UINT64 fence=0;unsigned mask=0,frame=0,step=0;};
  std::array<Slot,2> slots{};
  std::array<double,StageCount> last{};
};
