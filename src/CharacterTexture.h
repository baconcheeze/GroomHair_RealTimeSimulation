#pragma once
#include <d3d12.h>
#include <wrl/client.h>
#include <filesystem>
// One-time WIC decode/upload. All normal frame rendering samples GPU textures.
Microsoft::WRL::ComPtr<ID3D12Resource> LoadCharacterTextures(
    ID3D12Device*, ID3D12CommandQueue*, const std::filesystem::path&);
