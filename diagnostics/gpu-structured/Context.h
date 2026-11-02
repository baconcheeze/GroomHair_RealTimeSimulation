#pragma once
#include "../../src/GuideGpu.h"
#include <chrono>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
namespace gpuls
{
namespace fs = std::filesystem;
using guidegpu::check;
using Microsoft::WRL::ComPtr;
inline void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
inline void text(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << s;
    require(bool(f), "Output failed");
}
struct Context
{
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event{};
    UINT64 serial = 0, frequency = 0;
    std::string adapter;
    uint64_t driver = 0;
    bool fp64 = false, debug = false;
    D3D_SHADER_MODEL model = D3D_SHADER_MODEL_6_0;
    Context()
    {
#ifdef _DEBUG
        ComPtr<ID3D12Debug> d;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&d))))
        {
            d->EnableDebugLayer();
            debug = true;
        }
#endif
        ComPtr<IDXGIFactory6> factory;
        check(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
        for (UINT i = 0;; i++)
        {
            ComPtr<IDXGIAdapter1> a;
            if (factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a)) ==
                DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_ADAPTER_DESC1 desc{};
            check(a->GetDesc1(&desc));
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;
            if (SUCCEEDED(D3D12CreateDevice(a.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))))
            {
                for (auto c : std::wstring(desc.Description))
                    adapter += c < 128 ? char(c) : '?';
                LARGE_INTEGER v{};
                if (SUCCEEDED(a->CheckInterfaceSupport(__uuidof(IDXGIDevice), &v)))
                    driver = uint64_t(v.QuadPart);
                break;
            }
        }
        require(bool(device), "No hardware DX12 device");
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        check(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)));
        fp64 = options.DoublePrecisionFloatShaderOps != 0;
        for (unsigned v = 0x69; v >= 0x60; v--)
        {
            D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL(v)};
            if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))))
            {
                model = sm.HighestShaderModel;
                break;
            }
        }
        D3D12_COMMAND_QUEUE_DESC q{};
        check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)));
        check(queue->GetTimestampFrequency(&frequency));
        check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&list)));
        check(list->Close());
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        require(event != nullptr, "Fence event failed");
    }
    void submit(const std::function<void(ID3D12GraphicsCommandList *)> &record)
    {
        check(allocator->Reset());
        check(list->Reset(allocator.Get(), nullptr));
        record(list.Get());
        check(list->Close());
        ID3D12CommandList *lists[]{list.Get()};
        queue->ExecuteCommandLists(1, lists);
        check(queue->Signal(fence.Get(), ++serial));
        check(fence->SetEventOnCompletion(serial, event));
        require(WaitForSingleObject(event, 20000) == WAIT_OBJECT_0, "GPU diagnostic timeout (20s)");
        require(fence->GetCompletedValue() != UINT64_MAX, "Device removed");
    }
    void audit()
    {
        ComPtr<ID3D12InfoQueue> info;
        if (SUCCEEDED(device.As(&info)))
            for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); i++)
            {
                SIZE_T n = 0;
                info->GetMessage(i, nullptr, &n);
                std::vector<char> b(n);
                auto *m = reinterpret_cast<D3D12_MESSAGE *>(b.data());
                info->GetMessage(i, m, &n);
                if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                    throw std::runtime_error(m->pDescription);
            }
    }
    std::string json() const
    {
        std::ostringstream s;
        s << "{\"adapter\":\"" << adapter << "\",\"driverParts\":[" << ((driver >> 48) & 65535) << ','
          << ((driver >> 32) & 65535) << ',' << ((driver >> 16) & 65535) << ',' << (driver & 65535)
          << "],\"DoublePrecisionFloatShaderOps\":" << (fp64 ? "true" : "false")
          << ",\"shaderModelHex\":" << unsigned(model) << ",\"debugLayer\":" << (debug ? "true" : "false")
          << ",\"timestampFrequency\":" << frequency << '}';
        return s.str();
    }
    ~Context()
    {
        if (event)
            CloseHandle(event);
    }
};
inline ComPtr<ID3D12RootSignature> root(Context &c)
{
    D3D12_ROOT_PARAMETER p[3]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants.Num32BitValues = 4;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC d{};
    d.NumParameters = 3;
    d.pParameters = p;
    ComPtr<ID3DBlob> b, e;
    check(D3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e));
    ComPtr<ID3D12RootSignature> r;
    check(c.device->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&r)));
    return r;
}
inline ComPtr<ID3D12PipelineState> pipeline(Context &c, ID3D12RootSignature *r, const fs::path &p)
{
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    require(bool(f), "Missing executable-relative DXIL");
    std::vector<char> b(size_t(f.tellg()));
    f.seekg(0);
    f.read(b.data(), b.size());
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
    d.pRootSignature = r;
    d.CS = {b.data(), b.size()};
    ComPtr<ID3D12PipelineState> out;
    check(c.device->CreateComputePipelineState(&d, IID_PPV_ARGS(&out)));
    return out;
}
inline fs::path exeDir()
{
    wchar_t p[32768];
    GetModuleFileNameW(nullptr, p, 32768);
    return fs::path(p).parent_path();
}
} // namespace gpuls
