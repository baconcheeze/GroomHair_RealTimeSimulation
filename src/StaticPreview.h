#pragma once
#include "RestData.h"
#include <d3d12.h>
#include <wrl/client.h>
struct PreviewBuffer {
    Microsoft::WRL::ComPtr<ID3D12Resource> gpu,readback;
    const void* cpu{}; size_t bytes{}; std::string name;
    D3D12_RESOURCE_STATES state{};
};
class StaticPreview {
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> hairPso,headPso,hullPso;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> uploads;
    std::vector<PreviewBuffer> buffers;
    size_t selectedVB{},selectedIB{},originalVB{},originalIB{},hullVB{},hullIB{};
    UINT selectedIndices{},originalIndices{},hullIndices{},guideVertices{},renderVertices{};
public:
    ID3D12Resource* guideRestPosition() const {return buffers.at(0).gpu.Get();}
    ID3D12Resource* guideCurrentPosition() const {return buffers.at(1).gpu.Get();}
    ID3D12Resource* guideSegmentStarts() const {return buffers.at(2).gpu.Get();}
    bool guides=false,renderHair=true,head=true,hulls=false,original=false;
    void initialize(ID3D12Device* device,ID3D12GraphicsCommandList* list,const RestData& data,const std::filesystem::path& shaders);
    void releaseUploads() { uploads.clear(); }
    void draw(ID3D12GraphicsCommandList* list,const DirectX::XMFLOAT4X4& matrix);
    void recordValidation(ID3D12Device* device,ID3D12GraphicsCommandList* list,bool movingGuide=false);
    void finishValidation();
};
