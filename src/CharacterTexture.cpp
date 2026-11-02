#include "CharacterTexture.h"
#include <wincodec.h>
#include <vector>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace { void ck(HRESULT h) { if(FAILED(h)) throw std::runtime_error("Character texture upload/WIC failure: "+std::to_string(unsigned(h))); } }
ComPtr<ID3D12Resource> LoadCharacterTextures(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& dir) {
    HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(FAILED(co)&&co!=RPC_E_CHANGED_MODE)ck(co);
    struct Guard{bool active;~Guard(){if(active)CoUninitialize();}}guard{SUCCEEDED(co)};
    ComPtr<IWICImagingFactory> factory;
    ck(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory)));
    constexpr UINT side=2048, slices=6;
    D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width=side;desc.Height=side;desc.DepthOrArraySize=slices;desc.MipLevels=1;
    desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;desc.SampleDesc.Count=1;
    D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture;
    ck(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&texture)));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[slices];UINT64 total;
    device->GetCopyableFootprints(&desc,0,slices,0,footprints,nullptr,nullptr,&total);
    desc={};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=total;
    desc.Height=desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type=D3D12_HEAP_TYPE_UPLOAD;ComPtr<ID3D12Resource> upload;
    ck(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload)));
    BYTE* mapped;D3D12_RANGE noRead{};ck(upload->Map(0,&noRead,reinterpret_cast<void**>(&mapped)));
    for(UINT i=0;i<slices;++i){
        ComPtr<IWICBitmapDecoder> decoder;auto path=dir/(std::to_string(i)+".png");
        ck(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&decoder));
        ComPtr<IWICBitmapFrameDecode> frame;ck(decoder->GetFrame(0,&frame));
        ComPtr<IWICBitmapScaler> scaler;ck(factory->CreateBitmapScaler(&scaler));
        ck(scaler->Initialize(frame.Get(),side,side,WICBitmapInterpolationModeFant));
        ComPtr<IWICFormatConverter> converter;ck(factory->CreateFormatConverter(&converter));
        ck(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppRGBA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        ck(converter->CopyPixels(nullptr,footprints[i].Footprint.RowPitch,footprints[i].Footprint.RowPitch*side,mapped+footprints[i].Offset));
    }
    upload->Unmap(0,nullptr);
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    ck(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)));
    ck(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)));
    for(UINT i=0;i<slices;++i){
        D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=upload.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;src.PlacedFootprint=footprints[i];
        D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=texture.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;dst.SubresourceIndex=i;
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition={texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};
    list->ResourceBarrier(1,&barrier);ck(list->Close());ID3D12CommandList* lists[]={list.Get()};queue->ExecuteCommandLists(1,lists);
    ComPtr<ID3D12Fence> fence;ck(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)));
    HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)throw std::runtime_error("Texture fence event");
    ck(queue->Signal(fence.Get(),1));ck(fence->SetEventOnCompletion(1,event));WaitForSingleObject(event,INFINITE);CloseHandle(event);
    return texture;
}
