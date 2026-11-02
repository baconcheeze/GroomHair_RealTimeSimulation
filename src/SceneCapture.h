#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
class SceneCapture {
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT width{},height{};
public:
    void record(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12Resource* renderTarget);
    // Caller must wait for the submission fence before mapping.
    void save(const std::filesystem::path& png);
};
