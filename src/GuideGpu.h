#pragma once
#include "GuideState.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <functional>
#include <cstring>
using UploadList=std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>>;
using Submit=std::function<void(const std::function<void(ID3D12GraphicsCommandList*)>&)>;
namespace guidegpu {
void check(HRESULT h);
struct Buffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;
    size_t bytes{};
    void create(ID3D12Device* device,size_t countBytes,bool uav,const wchar_t* name);
    void transition(ID3D12GraphicsCommandList* list,D3D12_RESOURCE_STATES next);
    void upload(ID3D12Device* device,ID3D12GraphicsCommandList* list,const void* source,size_t size,UploadList& keep,D3D12_RESOURCE_STATES next);
    Microsoft::WRL::ComPtr<ID3D12Resource> readback(ID3D12Device* device,ID3D12GraphicsCommandList* list);
    D3D12_GPU_VIRTUAL_ADDRESS address() const {return resource->GetGPUVirtualAddress();}
};
void uavBarrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource);
std::vector<char> mappedBytes(ID3D12Resource* readback,size_t bytes);
template<class T> std::vector<T> read(Buffer& buffer,ID3D12Device* device,const Submit& submit,size_t count) {
    Microsoft::WRL::ComPtr<ID3D12Resource> staging;submit([&](auto* list){staging=buffer.readback(device,list);});auto raw=mappedBytes(staging.Get(),count*sizeof(T));std::vector<T> result(count);if(count)std::memcpy(result.data(),raw.data(),raw.size());return result;
}
struct State {
    // Borrow exactly the renderer's GuideCurrentPosition/GuideRestPosition allocations.
    Buffer position,restPosition,ranges,restOrientation,restLength,restBend,mass,radius,orientation,velocity;
    const GuideCpuState* cpu{};
    void initialize(ID3D12Device* device,ID3D12GraphicsCommandList* list,const GuideCpuState& source,ID3D12Resource* existingCurrent,ID3D12Resource* existingRest,UploadList& keep);
    void reset(ID3D12Device* device,ID3D12GraphicsCommandList* list,const std::vector<hair::F>& q,UploadList& keep);
    void verifyImmutable(ID3D12Device* device,const Submit& submit,bool requireRestOrientation,bool allowPositionChange=false,bool allowVelocityChange=false);
};
}
