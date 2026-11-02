#include "SceneCapture.h"
#include <wincodec.h>
#include <stdexcept>
#include <vector>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace { void ck(HRESULT h) { if(FAILED(h)) throw std::runtime_error("PNG capture failure: "+std::to_string(static_cast<unsigned>(h))); } }
void SceneCapture::record(ID3D12Device* device,ID3D12GraphicsCommandList* list,ID3D12Resource* target) {
    auto desc=target->GetDesc(); if(desc.Format!=DXGI_FORMAT_R8G8B8A8_UNORM) throw std::runtime_error("Capture expects RGBA8");
    width=static_cast<UINT>(desc.Width); height=desc.Height; UINT64 total{};
    device->GetCopyableFootprints(&desc,0,1,0,&footprint,nullptr,nullptr,&total);
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=total; buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ck(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition={target,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE}; list->ResourceBarrier(1,&b);
    D3D12_TEXTURE_COPY_LOCATION source{}; source.pResource=target; source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{}; destination.pResource=readback.Get(); destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; destination.PlacedFootprint=footprint;
    list->CopyTextureRegion(&destination,0,0,0,&source,nullptr);
    std::swap(b.Transition.StateBefore,b.Transition.StateAfter); list->ResourceBarrier(1,&b);
}
void SceneCapture::save(const std::filesystem::path& png) {
    if(!readback) throw std::runtime_error("No completed screenshot readback");
    if(png.has_parent_path()) std::filesystem::create_directories(png.parent_path());
    HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED); if(FAILED(com) && com!=RPC_E_CHANGED_MODE) ck(com);
    struct ComGuard { bool active; ~ComGuard(){ if(active) CoUninitialize(); } } guard{SUCCEEDED(com)};
    ComPtr<IWICImagingFactory> factory; ck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    ComPtr<IWICStream> stream; ck(factory->CreateStream(&stream)); ck(stream->InitializeFromFilename(png.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder; ck(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder)); ck(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; ck(encoder->CreateNewFrame(&frame,nullptr)); ck(frame->Initialize(nullptr)); ck(frame->SetSize(width,height));
    WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA; ck(frame->SetPixelFormat(&format)); if(format!=GUID_WICPixelFormat32bppBGRA) throw std::runtime_error("WIC BGRA encoding unsupported");
    // GetCopyableFootprints omits trailing padding after the final row.
    void* mapped; SIZE_T end=static_cast<SIZE_T>(footprint.Offset)+static_cast<SIZE_T>(footprint.Footprint.RowPitch)*(height-1)+static_cast<SIZE_T>(width)*4; D3D12_RANGE range{0,end}; ck(readback->Map(0,&range,&mapped));
    std::vector<BYTE> bgra(static_cast<size_t>(width)*4*height);
    for(UINT y=0;y<height;++y) {
        auto* src=static_cast<BYTE*>(mapped)+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch;
        auto* dst=bgra.data()+size_t(y)*width*4;
        for(UINT x=0;x<width;++x) { dst[x*4]=src[x*4+2]; dst[x*4+1]=src[x*4+1]; dst[x*4+2]=src[x*4]; dst[x*4+3]=src[x*4+3]; }
    }
    D3D12_RANGE written{0,0}; readback->Unmap(0,&written);
    ck(frame->WritePixels(height,width*4,static_cast<UINT>(bgra.size()),bgra.data())); ck(frame->Commit()); ck(encoder->Commit()); readback.Reset();
}
