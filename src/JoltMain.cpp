#include <Jolt/Jolt.h>
#include <Jolt/Compute/DX12/ComputeBufferDX12.h>
#include <Jolt/Compute/DX12/ComputeQueueDX12.h>
#include <Jolt/Compute/DX12/ComputeSystemDX12.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/RegisterTypes.h>
using namespace JPH; // Upstream Samples.h supplies this before Layers.h.
#include "JoltScene.h"
#include "OrbitCamera.h"
#include "SceneCapture.h"
#include "CharacterTexture.h"
#include "DenimJacket.h"
#include "LinkedHairMaterial.h"
#include "AirResistanceChecks.h"
#include "BodyPrimitiveChecks.h"
#include "GraphicsTimings.h"
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"
#include <Layers.h>
#include <array>
#include <chrono>
#include <d3d12sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <sstream>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
static void check(HRESULT h, const char *op) {
  if (FAILED(h))
    throw std::runtime_error(std::string(op) + " HRESULT " +
                             std::to_string(unsigned(h)));
}
static std::string utf8(const wchar_t *s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, 0, 0, 0, 0);
  std::string r(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, -1, r.data(), n, 0, 0);
  r.pop_back();
  return r;
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM,
                                                             LPARAM);
static std::ofstream *joltLog = nullptr;
static void joltTrace(const char *fmt, ...) {
  char text[4096];
  va_list args;
  va_start(args, fmt);
  vsnprintf(text, sizeof(text), fmt, args);
  va_end(args);
  if (joltLog) {
    *joltLog << "Jolt: " << text << '\n';
    joltLog->flush();
  }
  OutputDebugStringA(text);
}
struct Application {
  HWND window{};
  HINSTANCE instance = GetModuleHandleW(nullptr);
  bool registered = false, minimized = false, win32Ready = false,
       dx12Ready = false, warp = false, joltReady = false;
  UINT width = 1280, height = 800, pendingWidth = 0, pendingHeight = 0;
  unsigned rendered = 0, steps = 0, frameLimit = 0, stepLimit = 0, every = 30;
  bool running = false, stepOnce = false, animation = true, guides = false,
       renderHair = true, head = true, hulls = false, resetCheck = false,
       resetCapture = false, controlCheck = false, captureUI = false, bones = false;
  fs::path captureDir, logPath = "artifacts/jolt-session.log";
  fs::path profilePath;
  bool profileGpu=true;
  bool airCheck=false,bodyCheck=false;
  int animationClip=1; // Default to the newly imported dance; running remains separately controlled
  std::ofstream profileCsv,profileDispatchCsv;
  std::ofstream log;
  std::string adapterName, view = "quarter";
  bool playground=true,followCharacter=true,walkTest=false,pausedRequested=false;
  bool walkCheck=false,overrideInput=false;
  JPH::Vec3 overriddenInput=JPH::Vec3::sZero();
  DirectX::XMFLOAT3 followAnchor{};
  double fixedAccumulator=0,discardedWallTime=0;
  std::chrono::steady_clock::time_point previousFrameTime=std::chrono::steady_clock::now();
  std::array<float,8> stylingHolds{.5f,.5f,.5f,.5f,.5f,.5f,.5f,.5f};
  int materialRegion = 0;
  int guideDensity=0; // denominator 1,2,4,8 relative to current authored selection
  bool guideDensityCheck=false;
  bool hairSimulationEnabled = true;
  bool baseHair=true;
  int isolateRegion = -1; // Explicit authoring diagnostic; default draws all.
  int hairstyle = 2; // 0: authored bob, 1: original long, 2: purchased Alembic.
  bool showJacket = true;
  bool hideBody = false;
  ComPtr<IDXGIFactory6> factory;
  ComPtr<IDXGIAdapter1> adapter;
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<IDXGISwapChain3> swap;
  ComPtr<ID3D12DescriptorHeap> rtvHeap, dsvHeap, srvHeap;
  std::array<ComPtr<ID3D12Resource>, 2> targets;
  ComPtr<ID3D12Resource> depth, sceneColor;
  UINT sceneSamples = 1;
  std::array<ComPtr<ID3D12CommandAllocator>, 2> allocators;
  ComPtr<ID3D12GraphicsCommandList> commands;
  ComPtr<ID3D12Fence> fence;
  GraphicsTimings graphicsTimings;
  std::array<UINT64, 2> frameFence{};
  UINT64 serial = 0;
  HANDLE fenceEvent{};
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> lines, triangles, jacketTriangles, groundTriangles;
  ComPtr<ID3D12Resource> jacketPositions,jacketColors,jacketSurfaces,jacketIndices;
  unsigned jacketIndexCount=0;
  UINT rtvStride{}, srvStride{};
  std::vector<unsigned> freeSrv;
  ComPtr<ID3D12Resource> guideIndices, renderIndices, renderMaterials, hullVertices,
      characterColors, characterNormals, normalPoseBuffer, characterTexture;
  D3D12_GPU_DESCRIPTOR_HANDLE textureHandle{};
  std::vector<JPH::Mat44> displayInverseBind;
  std::vector<int32_t> boneParents;
  ComPtr<ID3D12Resource> boneVertices;
  unsigned boneVertexCount=0;
  unsigned guideVertices = 0, renderVertices = 0, hullVertexCount = 0;
  OrbitCamera camera;
  SceneCapture capture;
  JPH::Ref<JPH::ComputeSystemDX12> compute;
  JPH::Ref<JPH::ComputeQueue> computeQueue;
  BPLayerInterfaceImpl broad;
  ObjectVsBroadPhaseLayerFilterImpl objectBroad;
  ObjectLayerPairFilterImpl pairs;
  std::unique_ptr<JPH::PhysicsSystem> physics;
  std::unique_ptr<JPH::TempAllocatorImpl> temp;
  std::unique_ptr<JPH::JobSystemThreadPool> jobs;
  std::unique_ptr<hairlab::JoltScene> scene;
  double lastUpdateMs = 0;
  float delta = 1.f / 60;
  std::chrono::steady_clock::time_point start =
      std::chrono::steady_clock::now();
  fs::path executableDirectory() const {
    wchar_t p[32768];
    auto n = GetModuleFileNameW(nullptr, p, 32768);
    if (!n || n == 32768)
      throw std::runtime_error("Executable path");
    return fs::path(p).parent_path();
  }
  void write(const std::string &s) {
    if (log) {
      log << s << '\n';
      log.flush();
    }
    OutputDebugStringA((s + "\n").c_str());
  }
  void wait(UINT64 n) {
    if (fence->GetCompletedValue() < n) {
      check(fence->SetEventOnCompletion(n, fenceEvent), "Fence event");
      if (WaitForSingleObject(fenceEvent, 30000) != WAIT_OBJECT_0)
        throw std::runtime_error("GPU fence timeout");
    }
    if (fence->GetCompletedValue() == UINT64_MAX)
      throw std::runtime_error("Device removed");
  }
  void idle() {
    if (queue && fence && fenceEvent) {
      check(queue->Signal(fence.Get(), ++serial), "Signal");
      wait(serial);
    }
  }
  void parse() {
    int n;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &n);
    try {
      for (int i = 1; i < n; i++) {
        std::wstring k = argv[i];
        auto value = [&]() {
          if (i + 1 >= n)
            throw std::runtime_error("Missing CLI value");
          return std::wstring(argv[++i]);
        };
        auto fraction = [&]() {
          float f=std::stof(value());
          if(!std::isfinite(f)||f<0||f>1)throw std::runtime_error("Expected finite fraction in [0,1]");
          return f;
        };
        if (k == L"--steps") {
          stepLimit = std::stoul(value());
          running = true;
        }
        else if (k == L"--frames")
          frameLimit = std::stoul(value());
        else if (k == L"--profile-csv") profilePath=value();
        else if (k == L"--profile-cpu-only") profileGpu=false;
        else if(k==L"--air-check")airCheck=true;
        else if(k==L"--body-check")bodyCheck=true;
        else if(k==L"--guide-density-check")guideDensityCheck=true;
        else if(k==L"--guide-reduction"){
          auto v=value();if(v!=L"1"&&v!=L"2"&&v!=L"4"&&v!=L"8")throw std::runtime_error("Guide reduction must be 1|2|4|8");
          guideDensity=v==L"1"?0:v==L"2"?1:v==L"4"?2:3;
        }
        else if (k == L"--air-resistance") {
          const float coefficient=std::stof(value());
          if(!std::isfinite(coefficient)||coefficient<0||coefficient>20)throw std::runtime_error("Air resistance must be finite in [0,20] /s");
          hairlab::JoltScene::sAirResistance=coefficient;
          hairlab::JoltScene::sLongAirResistance=coefficient;
        }
        else if (k == L"--capture-dir")
          captureDir = value();
        else if (k == L"--capture-every")
          every = std::stoul(value());
        else if (k == L"--log")
          logPath = value();
        else if (k == L"--view") {
          auto v = value();
          const bool audit=v==L"audit-front"||v==L"audit-left"||v==L"audit-right"||v==L"audit-back"||v==L"audit-top"||v==L"audit-low-front"||v==L"audit-low-left"||v==L"audit-low-right"||v==L"audit-low-back";
          if (!audit && v != L"quarter" && v != L"portrait" && v != L"bust" && v != L"bust-side" && v != L"bust-back" && v != L"side" && v != L"back" && v != L"hair" && v != L"ear-left" && v != L"ear-right" && v != L"ear-back-left" && v != L"ear-back-right")
            throw std::runtime_error("Invalid view preset");
          view = utf8(v.c_str());
        } else if(k==L"--isolate-region") {
          isolateRegion=std::stoi(value());
          if(isolateRegion<0||isolateRegion>7)throw std::runtime_error("Region must be 0..7");
        } else if(k==L"--animation") {
          auto v=value();if(v!=L"run"&&v!=L"dance")throw std::runtime_error("Animation must be run|dance");
          animationClip=v==L"dance"?1:0;
        } else if(k==L"--walk-test") {
          walkTest=true;
        } else if(k==L"--walk-check") {
          walkCheck=true;view="quarter";
        } else if (k == L"--hairstyle") {
          auto v=value();
          if(v!=L"bob" && v!=L"long" && v!=L"metahuman")throw std::runtime_error("Hairstyle must be bob|long|metahuman");
          hairstyle=v==L"metahuman"?2:v==L"long"?1:0;
        } else if (k == L"--fringe-pose-root")
          hairlab::JoltScene::sFringeGlobalPose.mMin=fraction();
        else if (k == L"--fringe-pose-tip")
          hairlab::JoltScene::sFringeGlobalPose.mMax=fraction();
        else if (k == L"--fringe-pose-end") {
          float f=fraction();if(f<.01f)throw std::runtime_error("Pose end fraction must be at least 0.01");
          hairlab::JoltScene::sFringeGlobalPose.mMaxFraction=f;
        } else if (k == L"--fringe-skin-tip")
          hairlab::JoltScene::sFringeSkinGlobalPose.mMax=fraction();
        else if (k == L"--capture-ui")
          captureUI = true;
        else if (k == L"--control-check")
          controlCheck = true;
        else if (k == L"--paused") {
          running = false;pausedRequested=true;
        }
        else if(k==L"--run")running=true;
        else if (k == L"--no-animation")
          animation = false;
        else if (k == L"--no-jacket")
          showJacket = false;
        else if (k == L"--hide-body")
          hideBody = true;
        else if (k == L"--guides")
          guides = true;
        else if (k == L"--hair")
          renderHair = true;
        else if (k == L"--no-base-hair")baseHair=false;
        else if (k == L"--no-hair")
          renderHair = guides = false;
        else if (k == L"--hulls")
          hulls = true;
        else if (k == L"--bones")
          bones = true;
        else if (k == L"--reset-check")
          resetCheck = true;
        else
          throw std::runtime_error("Unknown Jolt-only option: " +
                                   utf8(k.c_str()));
      }
    } catch (...) {
      LocalFree(argv);
      throw;
    }
    LocalFree(argv);
    if(walkCheck)animationClip=0;
    if(walkTest){animationClip=0;view="quarter";if(!stepLimit)stepLimit=390;running=true;}
    playground=view=="quarter";
    if(playground&&!pausedRequested&&!frameLimit&&!controlCheck&&!walkCheck)running=true;
    if (every == 0 || stepLimit > 36000 || frameLimit > 36000)
      throw std::runtime_error("Bounded CLI limit");
    if (stepLimit && !running)
      throw std::runtime_error("--steps requires running");
    if (logPath.has_parent_path())
      fs::create_directories(logPath.parent_path());
    log.open(logPath);
    if (!log)
      throw std::runtime_error("Log open");
    if (!captureDir.empty())
      fs::create_directories(captureDir);
  }
  static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto *app =
        reinterpret_cast<Application *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
      app = static_cast<Application *>(
          reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (ImGui::GetCurrentContext() &&
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
      return 1;
    if (app && msg == WM_SIZE) {
      app->minimized = wp == SIZE_MINIMIZED;
      if (!app->minimized) {
        app->pendingWidth = LOWORD(lp);
        app->pendingHeight = HIWORD(lp);
      }
      return 0;
    }
    if (msg == WM_SYSCOMMAND && (wp & 0xfff0) == SC_KEYMENU)
      return 0;
    if (msg == WM_CLOSE) {
      PostQuitMessage(0);
      return 0;
    }
    if (msg == WM_DESTROY) {
      PostQuitMessage(0);
      return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
  D3D12_CPU_DESCRIPTOR_HANDLE rtv(unsigned index) {
    auto h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    h.ptr += index * rtvStride;
    return h;
  }
  void createTargets() {
    for (unsigned i = 0; i < 2; ++i) {
      check(swap->GetBuffer(i, IID_PPV_ARGS(&targets[i])), "GetBuffer");
      device->CreateRenderTargetView(targets[i].Get(), nullptr, rtv(i));
    }
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.SampleDesc.Count = sceneSamples;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    clear.DepthStencil.Depth = 1;
    check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                                          D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                          &clear, IID_PPV_ARGS(&depth)),
          "Create depth");
    device->CreateDepthStencilView(
        depth.Get(), nullptr, dsvHeap->GetCPUDescriptorHandleForHeapStart());
    sceneColor.Reset();
    if (sceneSamples > 1) {
      desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
      D3D12_CLEAR_VALUE sceneClear{};
      sceneClear.Format = desc.Format;
      sceneClear.Color[0] = sceneClear.Color[1] = sceneClear.Color[2] = .985f;
      sceneClear.Color[3] = 1;
      check(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc,
                  D3D12_RESOURCE_STATE_RENDER_TARGET, &sceneClear,
                  IID_PPV_ARGS(&sceneColor)), "Create multisample scene color");
      device->CreateRenderTargetView(sceneColor.Get(), nullptr, rtv(2));
    }
  }
  void initGraphics() {
    UINT flags = 0;
#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
      debug->EnableDebugLayer();
      flags = DXGI_CREATE_FACTORY_DEBUG;
      write("D3D12 debug layer enabled");
    } else
      write("D3D12 debug layer unavailable; continuing without it");
#endif
    HRESULT factoryResult = CreateDXGIFactory2(flags, IID_PPV_ARGS(&factory));
    if (FAILED(factoryResult) && flags) {
      write("DXGI debug factory unavailable; using ordinary factory");
      factoryResult = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    }
    check(factoryResult, "CreateDXGIFactory2");
    if (warp)
      check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),
            "EnumWarpAdapter");
    else {
      for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> candidate;
        HRESULT hr = factory->EnumAdapterByGpuPreference(
            i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&candidate));
        if (hr == DXGI_ERROR_NOT_FOUND)
          break;
        check(hr, "EnumAdapterByGpuPreference");
        DXGI_ADAPTER_DESC1 desc{};
        check(candidate->GetDesc1(&desc), "GetDesc1");
        if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
            SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_11_0,
                                        __uuidof(ID3D12Device), nullptr))) {
          adapter = candidate;
          break;
        }
      }
      if (!adapter)
        throw std::runtime_error("No DX12 hardware adapter. Use --warp "
                                 "explicitly for software rendering.");
    }
    DXGI_ADAPTER_DESC1 desc{};
    check(adapter->GetDesc1(&desc), "GetDesc1");
    adapterName = utf8(desc.Description);
    write("Adapter: " + adapterName +
          (warp ? " [WARP SOFTWARE]" : " [HARDWARE]"));
    check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                            IID_PPV_ARGS(&device)),
          "D3D12CreateDevice");
    sceneSamples = 4;
    for (auto format : {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_D32_FLOAT}) {
      D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS quality{};
      quality.Format = format; quality.SampleCount = 4;
      if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
                   &quality, sizeof(quality))) || !quality.NumQualityLevels)
        sceneSamples = 1;
    }
    write("Scene edge sampling: " + std::to_string(sceneSamples) + "x MSAA");
    D3D12_COMMAND_QUEUE_DESC q{};
    q.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    check(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)),
          "CreateCommandQueue");
    DXGI_SWAP_CHAIN_DESC1 sd{};
    sd.Width = width;
    sd.Height = height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> initial;
    check(factory->CreateSwapChainForHwnd(queue.Get(), window, &sd, nullptr,
                                          nullptr, &initial),
          "CreateSwapChainForHwnd");
    check(initial.As(&swap), "SwapChain3");
    check(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER),
          "MakeWindowAssociation");
    auto heap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible,
                    ComPtr<ID3D12DescriptorHeap> &out) {
      D3D12_DESCRIPTOR_HEAP_DESC h{};
      h.Type = type;
      h.NumDescriptors = count;
      h.Flags = visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
      check(device->CreateDescriptorHeap(&h, IID_PPV_ARGS(&out)),
            "CreateDescriptorHeap");
    };
    heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 3, false, rtvHeap);
    heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false, dsvHeap);
    heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, true, srvHeap);
    rtvStride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvStride = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (unsigned i = 64; i > 0; --i)
      freeSrv.push_back(i - 1);
    for (auto &a : allocators)
      check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                           IID_PPV_ARGS(&a)),
            "CreateCommandAllocator");
    check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                    allocators[0].Get(), nullptr,
                                    IID_PPV_ARGS(&commands)),
          "CreateCommandList");
    check(commands->Close(), "Close initial list");
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)),
          "CreateFence");
    fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent)
      throw std::runtime_error("CreateEvent failed");
    createTargets();
    createScene();
    if(!profilePath.empty() && profileGpu)graphicsTimings.Init(device.Get(),queue.Get(),profilePath.string()+".graphics.csv");
    initializeJolt();
    resetScene();
  }
  void initUi() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    win32Ready = ImGui_ImplWin32_Init(window);
    if (!win32Ready)
      throw std::runtime_error("ImGui Win32 init failed");
    ImGui_ImplDX12_InitInfo info{};
    info.Device = device.Get();
    info.CommandQueue = queue.Get();
    info.NumFramesInFlight = 2;
    info.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    info.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    info.SrvDescriptorHeap = srvHeap.Get();
    info.UserData = this;
    info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *i,
                                   D3D12_CPU_DESCRIPTOR_HANDLE *cpu,
                                   D3D12_GPU_DESCRIPTOR_HANDLE *gpu) {
      auto &a = *static_cast<Application *>(i->UserData);
      if (a.freeSrv.empty())
        throw std::runtime_error("ImGui SRV heap exhausted");
      unsigned index = a.freeSrv.back();
      a.freeSrv.pop_back();
      *cpu = a.srvHeap->GetCPUDescriptorHandleForHeapStart();
      *gpu = a.srvHeap->GetGPUDescriptorHandleForHeapStart();
      cpu->ptr += index * a.srvStride;
      gpu->ptr += index * a.srvStride;
    };
    info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *i,
                                  D3D12_CPU_DESCRIPTOR_HANDLE cpu,
                                  D3D12_GPU_DESCRIPTOR_HANDLE) {
      auto &a = *static_cast<Application *>(i->UserData);
      a.freeSrv.push_back(static_cast<unsigned>(
          (cpu.ptr - a.srvHeap->GetCPUDescriptorHandleForHeapStart().ptr) /
          a.srvStride));
    };
    dx12Ready = ImGui_ImplDX12_Init(&info);
    if (!dx12Ready)
      throw std::runtime_error("ImGui DX12 init failed");
    if (!ImGui_ImplDX12_CreateDeviceObjects())
      throw std::runtime_error("ImGui device objects failed");
  }

  ComPtr<ID3D12Resource> upload(const void *p, size_t size) {
    D3D12_HEAP_PROPERTIES h{};
    h.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = std::max(size_t(16), size);
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    check(device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d,
                                          D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&r)),
          "Upload buffer");
    void *dst;
    D3D12_RANGE noRead{};
    check(r->Map(0, &noRead, &dst), "Map upload");
    if (size)
      memcpy(dst, p, size);
    r->Unmap(0, nullptr);
    return r;
  }
  void createScene() {
    D3D12_ROOT_PARAMETER r[8]{};
    r[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    r[0].Constants = {0, 0, 44};
    for (unsigned i = 1; i < 5; i++) {
      r[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
      r[i].Descriptor.ShaderRegister = i - 1;
    }
    D3D12_ROOT_SIGNATURE_DESC desc{};
    r[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    r[5].Descriptor.ShaderRegister = 4;
    D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors=1;range.BaseShaderRegister=5;
    r[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    r[6].DescriptorTable={1,&range};r[6].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
    r[7].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;
    r[7].Descriptor.ShaderRegister=6;
    desc.NumParameters = 8;
    desc.pParameters = r;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, error;
    check(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                      &blob, &error),
          "Root serialize");
    check(device->CreateRootSignature(0, blob->GetBufferPointer(),
                                      blob->GetBufferSize(),
                                      IID_PPV_ARGS(&root)),
          "Root");
    auto compile = [&](const char *entry, const char *profile) {
      ComPtr<ID3DBlob> b, e;
      auto path = executableDirectory() / "shaders/JoltDisplay.hlsl";
      HRESULT h = D3DCompileFromFile(
          path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, entry,
          profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &b, &e);
      if (e)
        write(std::string((char *)e->GetBufferPointer(), e->GetBufferSize()));
      check(h, "Display shader");
      return b;
    };
    auto vs = compile("HairVS", "vs_5_0"), ps = compile("PS", "ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};
    p.pRootSignature = root.Get();
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    p.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    p.RasterizerState.FrontCounterClockwise = TRUE;
    p.RasterizerState.DepthClipEnable = TRUE;
    p.RasterizerState.MultisampleEnable = sceneSamples > 1;
    auto &b = p.BlendState.RenderTarget[0];
    b.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    b.SrcBlend = D3D12_BLEND_ONE;
    b.DestBlend = D3D12_BLEND_ZERO;
    b.BlendOp = D3D12_BLEND_OP_ADD;
    b.SrcBlendAlpha = D3D12_BLEND_ONE;
    b.DestBlendAlpha = D3D12_BLEND_ZERO;
    b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    b.LogicOp = D3D12_LOGIC_OP_NOOP;
    p.DepthStencilState.DepthEnable = TRUE;
    p.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    p.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    p.SampleMask = UINT_MAX;
    p.NumRenderTargets = 1;
    p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    p.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    p.SampleDesc.Count = sceneSamples;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    b.BlendEnable=TRUE;b.SrcBlend=D3D12_BLEND_SRC_ALPHA;b.DestBlend=D3D12_BLEND_INV_SRC_ALPHA;
    check(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&lines)),
          "Hair line PSO");
    vs = compile("HeadVS", "vs_5_0");
    ps = compile("HeadPS", "ps_5_0");
    p.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    p.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    b.BlendEnable=FALSE;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    check(device->CreateGraphicsPipelineState(&p, IID_PPV_ARGS(&triangles)),
          "Scalp PSO");
    vs=compile("JacketVS","vs_5_0");ps=compile("JacketPS","ps_5_0");
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; // Open jacket has visible lining.
    check(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&jacketTriangles)),"Denim jacket PSO");
    vs=compile("GroundVS","vs_5_0");ps=compile("GroundPS","ps_5_0");
    p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
    check(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&groundTriangles)),"Tile ground PSO");
  }
  void initializeJolt() {
    JPH::Trace = joltTrace;
    joltLog = &log;
#ifdef JPH_ENABLE_ASSERTS
    JPH::AssertFailed = [](const char *expr, const char *message,
                           const char *file, JPH::uint line) -> bool {
      joltTrace("ASSERT %s %s at %s:%u", expr, message ? message : "", file,
                line);
      throw std::runtime_error(std::string("Jolt assertion: ") + expr);
    };
#endif
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory;
    JPH::RegisterTypes();
    joltReady = true;
    temp = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);
    jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs,
                                                      JPH::cMaxPhysicsBarriers);
    compute = new JPH::ComputeSystemDX12;
    JPH::ComputeSystemResult result;
    if (!compute->Initialize(device.Get(), result))
      throw std::runtime_error(result.GetError().c_str());
    compute->mShaderLoader = [this](const char *name,
                                    JPH::Array<JPH::uint8> &bytes,
                                    JPH::String &error) {
      std::ifstream f(executableDirectory() / "JoltShaders" / name,
                      std::ios::binary | std::ios::ate);
      if (!f) {
        error = "Missing deployed Jolt shader";
        return false;
      }
      auto size = f.tellg();
      bytes.resize(size_t(size));
      f.seekg(0);
      f.read((char *)bytes.data(), size);
      return bool(f);
    };
    auto q = compute->CreateComputeQueue();
    if (q.HasError())
      throw std::runtime_error(q.GetError().c_str());
    computeQueue = q.Get();
    hairlab::JoltScene::assets = executableDirectory() / "Assets/Moca";
    write("Jolt v5.6.0 e77f175595e64cb44218cc9d9d56fc365ad0e36a; "
          "Moca character / authored and imported Alembic hair; Jolt GPU DX12 only");
  }
  void changeGuideDensity(int selection){
    if(selection<0||selection>3)throw std::runtime_error("Invalid guide density selection");
    running=false;stepOnce=false;guideDensity=selection;
    resetScene(true);
    write("Guide selection rebuilt: 1/"+std::to_string(1u<<guideDensity)+", guides="+std::to_string(scene->settings().mSimStrands.size())+", render="+std::to_string(scene->settings().mRenderStrands.size()));
  }
  void resetScene(bool preserveMaterials=false) {
    JPH::Array<JPH::HairSettings::Material> overrides;
    if(preserveMaterials && scene)overrides=scene->settings().mMaterials;
    if(!preserveMaterials){stylingHolds.fill(.5f);materialRegion=0;}
    idle();
    if (computeQueue)
      computeQueue->Wait();
    scene.reset();
    physics = std::make_unique<JPH::PhysicsSystem>();
    physics->Init(10240, 0, 65536, 10240, broad, objectBroad, pairs);
    scene = std::make_unique<hairlab::JoltScene>();
    scene->bob=hairstyle==0;
    scene->metaHuman=hairstyle==2;
    scene->walkingEnabled=playground;
    scene->danceClip=animationClip==1;
    scene->guideReduction=1u<<guideDensity;
    scene->materialOverrides=std::move(overrides);
    scene->mPhysicsSystem = physics.get();
    scene->mBodyInterface = &physics->GetBodyInterface();
    scene->mComputeSystem = compute;
    scene->mComputeQueue = computeQueue;
    scene->Initialize();
    scene->profileEnabled=!profilePath.empty();
    scene->hair()->mProfileCpuTimings=scene->profileEnabled;
    auto *profileQueue=static_cast<JPH::ComputeQueueDX12*>(computeQueue.GetPtr());
    if(!profileQueue->EnableTimestamps(scene->profileEnabled && profileGpu))throw std::runtime_error("GPU timestamp initialization failed");
    if(scene->profileEnabled)write("Profile queue frequency: "+std::to_string(profileQueue->GetTimestampFrequency())+" Hz; GPU timestamps="+(profileGpu?"on":"off"));
    scene->animate = animation;
    scene->hairSimulationEnabled = hairSimulationEnabled;
    steps = 0;
    fixedAccumulator=0;discardedWallTime=0;previousFrameTime=std::chrono::steady_clock::now();
    static_assert(sizeof(JPH_HairPosition) == 28 &&
                  offsetof(JPH_HairPosition, mPosition) == 0 &&
                  offsetof(JPH_HairPosition, mRotation) == 12);
    static_assert(sizeof(JPH::Float3) == 12 &&
                  sizeof(JPH::IndexedTriangleNoMaterial) == 12);
    const auto &s = scene->settings();
    std::ifstream colorsFile(hairlab::JoltScene::assets / "colors.bin",
                             std::ios::binary);
    uint32_t colorCount = 0;
    colorsFile.read(reinterpret_cast<char *>(&colorCount), 4);
    if (!colorsFile || colorCount != s.mScalpVertices.size())
      throw std::runtime_error("Moca color/mesh count mismatch");
    std::vector<DirectX::XMFLOAT4> palette(colorCount);
    colorsFile.read(reinterpret_cast<char *>(palette.data()),
                    palette.size() * 16);
    if (!colorsFile)
      throw std::runtime_error("Truncated Moca colors");
    characterColors = upload(palette.data(), palette.size() * 16);
    struct Surface { float uv[2]; uint32_t texture, flags; float normal[4]; uint32_t joints[4]; float weights[4]; };
    static_assert(sizeof(Surface)==64 && offsetof(Surface,joints)==32 && offsetof(Surface,weights)==48);
    std::ifstream surfaceFile(hairlab::JoltScene::assets / "surface.bin",std::ios::binary);
    uint32_t count=0;surfaceFile.read(reinterpret_cast<char*>(&count),4);
    if(count!=colorCount)throw std::runtime_error("Surface count mismatch");
    std::vector<Surface> surfaces(count);
    surfaceFile.read(reinterpret_cast<char*>(surfaces.data()),surfaces.size()*sizeof(Surface));
    if(!surfaceFile)throw std::runtime_error("Truncated surface data");
    // Display-only head mask. Do not modify the on-disk skin or physics data.
    for(auto &surface:surfaces){float headWeight=0;for(unsigned k=0;k<4;++k)if(surface.joints[k]==scene->mHeadJointIdx)headWeight+=surface.weights[k];if(headWeight>.5f)surface.flags|=8u;}

    std::ifstream bindFile(hairlab::JoltScene::assets / "binds.bin",std::ios::binary);
    bindFile.read(reinterpret_cast<char*>(&count),4);
    if(count!=s.mScalpInverseBindPose.size())throw std::runtime_error("Display rig count mismatch");
    displayInverseBind.resize(count);
    bindFile.read(reinterpret_cast<char*>(displayInverseBind.data()),count*sizeof(JPH::Mat44));
    if(!bindFile)throw std::runtime_error("Truncated display bind data");
    std::ifstream boneFile(hairlab::JoltScene::assets / "skeleton.bin",std::ios::binary);
    uint32_t boneCount=0;boneFile.read(reinterpret_cast<char*>(&boneCount),4);
    if(boneCount!=count)throw std::runtime_error("Skeleton count mismatch");
    boneParents.resize(count);boneFile.read(reinterpret_cast<char*>(boneParents.data()),count*4);
    if(!boneFile)throw std::runtime_error("Truncated skeleton");
    for(auto p:boneParents)if(p < -1 || p>=int32_t(count))throw std::runtime_error("Invalid bone parent");
    // Display-only body tag, derived from the actual rig, not world height.
    // Keep the head, face/eye descendants and repaired scalp through motion.
    std::vector<bool> headBones(count,false);
    for (uint32_t j=0;j<count;++j) {
      int32_t ancestor=int32_t(j);
      for (uint32_t guard=0;ancestor>=0 && guard<count;++guard) {
        if (ancestor==int32_t(scene->mHeadJointIdx)) {headBones[j]=true;break;}
        ancestor=boneParents[ancestor];
      }
    }
    for (auto &surface:surfaces) {
      float headWeight=0;
      for (unsigned k=0;k<4;++k) {
        if (surface.joints[k]>=count)throw std::runtime_error("Invalid display skin joint");
        if (headBones[surface.joints[k]])headWeight+=surface.weights[k];
      }
      if (headWeight<.5f)surface.flags|=2u;
    }
    characterNormals=upload(surfaces.data(),surfaces.size()*sizeof(Surface));
    normalPoseBuffer=upload(displayInverseBind.data(),count*sizeof(JPH::Mat44));
    if(!jacketPositions){
      auto garment=DenimJacketAsset::Load(hairlab::JoltScene::assets/"denim-jacket.bin",count);
      jacketPositions=upload(garment.positions.data(),garment.positions.size()*12);
      jacketColors=upload(garment.colors.data(),garment.colors.size()*16);
      jacketSurfaces=upload(garment.surfaces.data(),garment.surfaces.size()*64);
      jacketIndices=upload(garment.indices.data(),garment.indices.size()*4);
      jacketIndexCount=unsigned(garment.indices.size());
      write("Denim jacket loaded: vertices="+std::to_string(garment.positions.size())+", triangles="+std::to_string(jacketIndexCount/3));
    }
    if(!characterTexture){
      characterTexture=LoadCharacterTextures(device.Get(),queue.Get(),hairlab::JoltScene::assets / "textures");
      if(freeSrv.empty())throw std::runtime_error("Character SRV exhausted");
      unsigned index=freeSrv.back();freeSrv.pop_back();
      auto cpu=srvHeap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=index*srvStride;
      textureHandle=srvHeap->GetGPUDescriptorHandleForHeapStart();textureHandle.ptr+=index*srvStride;
      D3D12_SHADER_RESOURCE_VIEW_DESC desc{};desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
      desc.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2DARRAY;desc.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      desc.Texture2DArray.MipLevels=1;desc.Texture2DArray.ArraySize=6;
      device->CreateShaderResourceView(characterTexture.Get(),&desc,cpu);
    }
    std::vector<unsigned> gi, ri;
    for (unsigned i = 0; i < s.mSimStrands.size(); i++)
      for (unsigned v = 0; v + 1 < s.mSimStrands[i].VertexCount(); v++) {
        gi.push_back(i + v * unsigned(s.mSimStrands.size()));
        gi.push_back(i + (v + 1) * unsigned(s.mSimStrands.size()));
      }
    std::vector<uint32_t> displayMaterials(s.mRenderStrands.size());
    if(isolateRegion>=0 || scene->metaHuman){
      if(!scene->metaHuman)throw std::runtime_error("Region isolation requires MetaHuman groom");
      std::ifstream file(hairlab::JoltScene::assets/"metahuman.materials",std::ios::binary);uint32_t n=0;
      file.read(reinterpret_cast<char*>(&n),4);
      if(n!=displayMaterials.size())throw std::runtime_error("Region display count mismatch");
      file.read(reinterpret_cast<char*>(displayMaterials.data()),displayMaterials.size()*4);
      if(!file)throw std::runtime_error("Region display data truncated");
    }
    renderMaterials=upload(displayMaterials.data(),displayMaterials.size()*4);
    for(size_t strandIndex=0;strandIndex<s.mRenderStrands.size();++strandIndex) {
      if(isolateRegion>=0 && displayMaterials[strandIndex]!=uint32_t(isolateRegion))continue;
      const auto &strand=s.mRenderStrands[strandIndex];
      for (unsigned v = strand.mStartVtx; v + 1 < strand.mEndVtx; v++) {
        ri.push_back(v);
        ri.push_back(v + 1);
      }
    }
    if(ri.empty())throw std::runtime_error("Selected display region has no strands");
    guideVertices = unsigned(gi.size());
    renderVertices = unsigned(ri.size());
    guideIndices = upload(gi.data(), gi.size() * 4);
    renderIndices = upload(ri.data(), ri.size() * 4);
    auto bounds = s.mSimulationBounds;
    auto center = scene->hair()->GetWorldTransform() * bounds.GetCenter();
    configureCamera();
    write("Moca scene loaded: guides=" + std::to_string(s.mSimStrands.size()) +
          ", render=" + std::to_string(s.mRenderStrands.size()) +
          ", scalp vertices=" + std::to_string(s.mScalpVertices.size()) +
          ", triangles=" + std::to_string(s.mScalpTriangles.size()) +
          ", joints=" + std::to_string(s.mScalpInverseBindPose.size()) +
          ", frames=" + std::to_string(scene->frameCount()) +
          ", hulls=" + std::to_string(scene->mAttachedBodies.size()));
    write("GPU body capsules="+std::to_string(scene->bodyHullCount)+"; CPU convex bodies: head/ears only");
  }
  void configureCamera() {
    if(playground&&view=="quarter"){
      DirectX::XMFLOAT4X4 identity;DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
      camera.verticalFov=DirectX::XM_PIDIV4;
      auto p=scene->locomotion.position;
      followAnchor={p.GetX(),.68f,-p.GetZ()};
      camera.setHair(followAnchor,.84f,identity,float(width)/height);
      camera.nearClip=.04f;camera.farClip=150;camera.minDistance=.22f;camera.maxDistance=35;
      camera.initialYaw=DirectX::XM_PI-.65f;camera.initialPitch=.48f;
      camera.initialDistance=2.65f;camera.reset();return;
    }
    DirectX::XMFLOAT4X4 identity;DirectX::XMStoreFloat4x4(&identity,DirectX::XMMatrixIdentity());
    if(view.starts_with("audit-")) {
      camera.verticalFov=DirectX::XMConvertToRadians(35.f);
      auto local=scene->mFaceAnimation[0][scene->mHeadJointIdx].InversedRotationTranslation()*JPH::Vec3(0,1.278f,.027f);
      auto p=scene->hair()->GetWorldTransform()*local;
      camera.setHair({p.GetX(),p.GetY(),-p.GetZ()},.134f,identity,float(width)/height);
      camera.initialYaw=view.ends_with("left")?-DirectX::XM_PIDIV2:view.ends_with("right")?DirectX::XM_PIDIV2:view.ends_with("back")?0.f:DirectX::XM_PI;
      camera.initialPitch=view=="audit-top"?1.28f:view.starts_with("audit-low-")?-.62f:.06f;
      if(playground){auto a=scene->locomotion.position;followAnchor={a.GetX(),.68f,-a.GetZ()};}
      camera.reset();return;
    }
    if(view.starts_with("ear-")) {
      const bool left=view.ends_with("left"),rear=view.starts_with("ear-back-");
      camera.verticalFov=DirectX::XMConvertToRadians(35.f);
      auto local=scene->mFaceAnimation[0][scene->mHeadJointIdx].InversedRotationTranslation()*JPH::Vec3(left?-.052f:.052f,1.258f,.026f);
      auto point=scene->hair()->GetWorldTransform()*local;
      camera.setHair({point.GetX(),point.GetY(),-point.GetZ()},.067f,identity,float(width)/height);
      if(playground){auto p=scene->locomotion.position;followAnchor={p.GetX(),.68f,-p.GetZ()};}
      camera.initialYaw=(left?-1.f:1.f)*(rear?DirectX::XM_PI/3:DirectX::XM_PIDIV2);
      camera.initialPitch=.06f;camera.reset();return;
    }
    const bool beauty=view=="portrait";
    const bool outfit=view=="bust"||view=="bust-side"||view=="bust-back";
    camera.verticalFov=beauty?DirectX::XMConvertToRadians(28.f):DirectX::XM_PIDIV4;
    camera.setHair({beauty?.026f:0.f,outfit?1.13f:beauty?1.265f:scene->metaHuman?1.16f:1.26f,0},
                   outfit?.32f:beauty?.095f:scene->metaHuman?.32f:.19f,identity,float(width)/height);
    if(playground){
      auto p=scene->locomotion.position;followAnchor={p.GetX(),.68f,-p.GetZ()};
      camera.initialTarget.x+=p.GetX();camera.initialTarget.y-=scene->locomotion.floorBindY;
      camera.initialTarget.z-=p.GetZ();
    }
    camera.yaw=(view=="back"||view=="bust-back")?0.f:(view=="side"||view=="bust-side")?DirectX::XM_PIDIV2:DirectX::XM_PI-.32f;
    camera.pitch=.025f;camera.initialYaw=camera.yaw;camera.initialPitch=camera.pitch;
  }
  void updateHulls() {
    std::vector<JPH::Float3> v;
    for (auto attached : scene->mAttachedBodies) {
      auto shape = physics->GetBodyInterface().GetShape(attached.mBodyID);
      auto *h = static_cast<const JPH::ConvexHullShape *>(shape.GetPtr());
      auto tr = physics->GetBodyInterface().GetCenterOfMassTransform(
          attached.mBodyID);
      for (unsigned f = 0; f < h->GetNumFaces(); f++) {
        unsigned count = h->GetNumVerticesInFace(f);
        std::vector<unsigned> ids(count);
        h->GetFaceVertices(f, count, ids.data());
        for (unsigned j = 0; j < count; j++) {
          JPH::Float3 a, b;
          (tr * h->GetPoint(ids[j])).StoreFloat3(&a);
          (tr * h->GetPoint(ids[(j + 1) % count])).StoreFloat3(&b);
          v.push_back(a);
          v.push_back(b);
        }
      }
    }
    hullVertexCount = unsigned(v.size());
    hullVertices = upload(v.data(), v.size() * sizeof(JPH::Float3));
  }
  void drawHair() {
    auto *hair = scene->hair();
    struct Constants {
      DirectX::XMFLOAT4X4 vp, world;
      float color[4];
      DirectX::XMFLOAT4 eye;
      DirectX::XMFLOAT4 hairStyle;
    } c{};
    static_assert(sizeof(Constants)==44*4);
    DirectX::XMStoreFloat4(&c.eye,camera.eye());
    c.hairStyle.w=baseHair?1.f:0.f;
    c.eye.w=hideBody?0.f:1.f; // Draw constant: body visibility, xyz remains camera.
    DirectX::XMStoreFloat4x4(&c.vp, camera.matrix(float(width) / height));
    auto model = hair->GetWorldTransform();
    for (unsigned i = 0; i < 4; i++)
      model.GetColumn4(i).StoreFloat4(
          reinterpret_cast<JPH::Float4 *>(&c.world) + i);
    commands->SetGraphicsRootSignature(root.Get());
    commands->SetGraphicsRootShaderResourceView(7,renderMaterials->GetGPUVirtualAddress());
    auto bind = [&](JPH::ComputeBuffer *buffer) {
      auto *b = static_cast<JPH::ComputeBufferDX12 *>(buffer);
      b->Barrier(commands.Get(),
                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      commands->SetGraphicsRootShaderResourceView(
          1, b->GetResourceGPU()->GetGPUVirtualAddress());
      c.color[3] = float(buffer->GetStride());
    };
    auto color = [&](float r, float g, float b) {
      c.color[0] = r;
      c.color[1] = g;
      c.color[2] = b;
      commands->SetGraphicsRoot32BitConstants(0, 44, &c, 0);
    };
    if (head) {
      graphicsTimings.Begin(commands.Get(),GraphicsTimings::Head);
      bind(hair->GetScalpVerticesCB());
      auto *indices =
          static_cast<JPH::ComputeBufferDX12 *>(hair->GetScalpTrianglesCB());
      indices->Barrier(commands.Get(), D3D12_RESOURCE_STATE_INDEX_BUFFER);
      auto *r = indices->GetResourceGPU();
      D3D12_INDEX_BUFFER_VIEW ib{r->GetGPUVirtualAddress(),
                                 unsigned(r->GetDesc().Width),
                                 DXGI_FORMAT_R32_UINT};
      commands->IASetIndexBuffer(&ib);
      commands->SetPipelineState(triangles.Get());
      commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
      commands->SetGraphicsRootShaderResourceView(
          3, characterColors->GetGPUVirtualAddress());
      commands->SetGraphicsRootShaderResourceView(
          4, characterNormals->GetGPUVirtualAddress());
      std::vector<JPH::Mat44> normalMatrices(displayInverseBind.size());
      for(unsigned j=0;j<normalMatrices.size();++j)
        normalMatrices[j]=scene->worldPose()[j]*displayInverseBind[j];
      void *mapped;D3D12_RANGE noRead{};
      check(normalPoseBuffer->Map(0,&noRead,&mapped),"Map rig normal poses");
      memcpy(mapped,normalMatrices.data(),normalMatrices.size()*sizeof(JPH::Mat44));
      normalPoseBuffer->Unmap(0,nullptr);
      commands->SetGraphicsRootShaderResourceView(5,normalPoseBuffer->GetGPUVirtualAddress());
      ID3D12DescriptorHeap* heaps[]={srvHeap.Get()};commands->SetDescriptorHeaps(1,heaps);
      commands->SetGraphicsRootDescriptorTable(6,textureHandle);
      color(1, 1, 1);
      commands->DrawIndexedInstanced(
          unsigned(scene->settings().mScalpTriangles.size()) * 3, 1, 0, 0, 0);
      indices->Barrier(commands.Get(),
                       D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
      graphicsTimings.End(commands.Get(),GraphicsTimings::Head);
      if(showJacket && !hideBody){
        graphicsTimings.Begin(commands.Get(),GraphicsTimings::Jacket);
        commands->SetPipelineState(jacketTriangles.Get());
        commands->SetGraphicsRootShaderResourceView(1,jacketPositions->GetGPUVirtualAddress());
        commands->SetGraphicsRootShaderResourceView(3,jacketColors->GetGPUVirtualAddress());
        commands->SetGraphicsRootShaderResourceView(4,jacketSurfaces->GetGPUVirtualAddress());
        D3D12_INDEX_BUFFER_VIEW garmentIB{jacketIndices->GetGPUVirtualAddress(),jacketIndexCount*4,DXGI_FORMAT_R32_UINT};
        commands->IASetIndexBuffer(&garmentIB);
        commands->DrawIndexedInstanced(jacketIndexCount,1,0,0,0);
        graphicsTimings.End(commands.Get(),GraphicsTimings::Jacket);
      }
    }
    commands->SetPipelineState(lines.Get());
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    if (renderHair) {
      graphicsTimings.Begin(commands.Get(),GraphicsTimings::RenderHair);
      c.hairStyle={scene->metaHuman?1.f:0.f,.32f,.22f,0.f};
      bind(hair->GetRenderPositionsCB());
      commands->SetGraphicsRootShaderResourceView(7,renderMaterials->GetGPUVirtualAddress());
      commands->SetGraphicsRootShaderResourceView(
          2, renderIndices->GetGPUVirtualAddress());
      color(.006f,.006f,.007f); // Black hair; retain highlights for strand readability.
      commands->DrawInstanced(renderVertices, 1, 0, 0);
      graphicsTimings.End(commands.Get(),GraphicsTimings::RenderHair);
      c.hairStyle.x=0.f;
    }
    if (guides) {
      graphicsTimings.Begin(commands.Get(),GraphicsTimings::Guides);
      bind(hair->GetPositionsCB());
      commands->SetGraphicsRootShaderResourceView(
          2, guideIndices->GetGPUVirtualAddress());
      color(.05f, .9f, 1);
      commands->DrawInstanced(guideVertices, 1, 0, 0);
      graphicsTimings.End(commands.Get(),GraphicsTimings::Guides);
    }
    if (hulls) {
      graphicsTimings.Begin(commands.Get(),GraphicsTimings::Colliders);
      commands->SetGraphicsRootShaderResourceView(
          1, hullVertices->GetGPUVirtualAddress());
      DirectX::XMStoreFloat4x4(&c.world, DirectX::XMMatrixIdentity());
      c.color[3] = -12;
      color(1, .4f, .1f);
      commands->DrawInstanced(hullVertexCount, 1, 0, 0);
      if(hair->GetBodyPrimitiveCount()>0){
        bind(hair->GetBodyWireCB());
        for(unsigned i=0;i<4;++i) model.GetColumn4(i).StoreFloat4(reinterpret_cast<JPH::Float4 *>(&c.world)+i);
        c.color[3]=-12;color(.25f,.9f,.35f);
        commands->DrawInstanced(hair->GetBodyPrimitiveCount()*200,1,0,0);
      }

      graphicsTimings.End(commands.Get(),GraphicsTimings::Colliders);
    }
    if(bones && boneVertexCount){
      graphicsTimings.Begin(commands.Get(),GraphicsTimings::Bones);
      commands->SetGraphicsRootShaderResourceView(1,boneVertices->GetGPUVirtualAddress());
      DirectX::XMStoreFloat4x4(&c.world,DirectX::XMMatrixIdentity());
      c.color[3]=-12;color(.95f,.7f,.1f);
      commands->DrawInstanced(boneVertexCount,1,0,0);
      graphicsTimings.End(commands.Get(),GraphicsTimings::Bones);
    }
  }
  void auditDebug() {
    ComPtr<ID3D12InfoQueue> q;
    if (SUCCEEDED(device.As(&q))) {
      bool failed = false;
      for (UINT64 i = 0; i < q->GetNumStoredMessagesAllowedByRetrievalFilter();
           i++) {
        SIZE_T n{};
        q->GetMessage(i, nullptr, &n);
        std::vector<char> b(n);
        auto *m = (D3D12_MESSAGE *)b.data();
        q->GetMessage(i, m, &n);
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
          write(m->pDescription);
        failed |= m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR;
      }
      if (failed)
        throw std::runtime_error("DX12 debug-layer errors");
      write("D3D12 debug audit: no errors");
    }
  }
  void saveState(const fs::path &path) {
    // Explicit bounded validation/capture only. Normal rendering never maps
    // geometry.
    idle(); // Controller/API callers may still have graphics reads in flight.
    scene->hair()->ReadBackGPUState(computeQueue);
    computeQueue->Wait();
    scene->hair()->LockReadBackBuffers();
    const auto &s = scene->settings();
    auto *g = scene->hair()->GetPositions();
    auto *r = scene->hair()->GetRenderPositions();
    auto *scalp = scene->hair()->GetScalpVertices();
    std::ofstream o(path);
    o << std::setprecision(9) << "{\"character\":\"Moca\",\"step\":" << steps
      << ",\"hairstyle\":\"" << (hairstyle==2?"metahuman":hairstyle==0?"bob":"long") << '"'
      << ",\"displayRegionFilter\":" << isolateRegion
      << ",\"denimJacket\":" << (showJacket?"true":"false")
      << ",\"hideBody\":" << (hideBody?"true":"false")
      << ",\"baseHair\":" << (baseHair?"true":"false")
      << ",\"jacketTriangles\":" << jacketIndexCount/3
      << ",\"playground\":" << (playground?"true":"false")
      << ",\"animationClip\":" << animationClip << ",\"clipPhase\":" << scene->locomotion.phase
      << ",\"animationFrame\":" << scene->frame()
      << ",\"hairSimulationEnabled\":" << (scene->hairSimulationEnabled ? "true" : "false")
      << ",\"guideReduction\":" << scene->guideReduction
      << ",\"guides\":" << s.mSimStrands.size()
      << ",\"render\":" << s.mRenderStrands.size()
      << ",\"hulls\":" << scene->mAttachedBodies.size() << ",\"dt\":" << delta
      << ",\"bodyPrimitiveCount\":" << scene->hair()->GetBodyPrimitiveCount() << ",\"bodyHullCount\":" << 0
      << ",\"iterationsPerSecond\":" << s.mNumIterationsPerSecond << ",\"materials\":[";
    auto gradient=[&](const hairlab::JoltScene::Gradient& v){o<<'['<<v.mMin<<','<<v.mMax<<','<<v.mMinFraction<<','<<v.mMaxFraction<<']';};
    for(unsigned i=0;i<s.mMaterials.size();++i){
      if(i)o<<',';const auto& m=s.mMaterials[i];
      o<<"{\"globalPose\":";gradient(m.mGlobalPose);
      o<<",\"collisionEnabled\":"<<(m.mEnableCollision?"true":"false");
      o<<",\"skinGlobalPose\":";gradient(m.mSkinGlobalPose);
      o<<",\"worldTransformInfluence\":";gradient(m.mWorldTransformInfluence);
      o<<",\"hairRadius\":";gradient(m.mHairRadius);
      o<<",\"bendCompliance\":"<<m.mBendCompliance<<",\"linearDamping\":"<<m.mLinearDamping;
      o<<",\"airResistance\":"<<m.mAirResistance<<",\"friction\":"<<m.mFriction;
      o<<",\"angularDamping\":"<<m.mAngularDamping<<",\"gridVelocityFactor\":";gradient(m.mGridVelocityFactor);
      o<<",\"bendComplianceMultiplier\":["<<m.mBendComplianceMultiplier.x<<','<<m.mBendComplianceMultiplier.y<<','<<m.mBendComplianceMultiplier.z<<','<<m.mBendComplianceMultiplier.w<<"]}";
    }
    o<<"],\"guideMaterials\":[";
    for(unsigned i=0;i<s.mSimStrands.size();++i){if(i)o<<',';o<<s.mSimStrands[i].mMaterialIndex;}
    o<<"],\"guideFreeVertices\":[";
    for(unsigned i=0;i<s.mSimStrands.size();++i){if(i)o<<',';const auto& st=s.mSimStrands[i];unsigned free=0;for(unsigned v=st.mStartVtx;v<st.mEndVtx;++v)free+=s.mSimVertices[v].mInvMass>0;o<<free;}
    o<<"],\"guideRestTips\":[";
    for(unsigned i=0;i<s.mSimStrands.size();++i){if(i)o<<',';auto p=s.mSimVertices[s.mSimStrands[i].mEndVtx-1].mPosition;o<<'['<<p.x<<','<<p.y<<','<<p.z<<']';}
    o<<"],\"guideRootRotations\":[";
    const auto* rotations=scene->hair()->GetRotations();
    for(unsigned i=0;i<s.mSimStrands.size();++i){if(i)o<<',';auto q=rotations[s.mSimStrands[i].mStartVtx];o<<'['<<q.GetX()<<','<<q.GetY()<<','<<q.GetZ()<<','<<q.GetW()<<']';}
    o<<"],\"guideTips\":[";
    bool finite = true;
    for (unsigned i = 0; i < s.mSimStrands.size(); i++) {
      auto p = g[s.mSimStrands[i].mEndVtx - 1];
      finite &= std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
      if (i)
        o << ',';
      o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
    }
    o << "],\"renderTips\":[";
    for (unsigned i = 0; i < s.mRenderStrands.size(); i++) {
      auto p = r[s.mRenderStrands[i].mEndVtx - 1];
      finite &= std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
      if (i)
        o << ',';
      o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
    }
    o << "],\"scalpSample\":[";
    for (unsigned i = 0; i < 16; i++) {
      auto p = scalp[i];
      if (i)
        o << ',';
      o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
    }
    o << "],\"scalpVertices\":[";
    for (unsigned i = 0; i < s.mScalpVertices.size(); i++) {
      auto p = scalp[i];
      if (i)
        o << ',';
      o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
      finite &= std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    double rootError = 0;
    o << "],\"guideRoots\":[";
    for (unsigned i = 0; i < s.mSimStrands.size(); i++) {
      auto p = g[s.mSimStrands[i].mStartVtx];
      auto sp = s.mSkinPoints[i];
      auto tri = s.mScalpTriangles[sp.mTriangleIndex];
      JPH::Vec3 expected =
          sp.mU * JPH::Vec3(scalp[tri.mIdx[0]]) +
          sp.mV * JPH::Vec3(scalp[tri.mIdx[1]]) +
          (1.f - sp.mU - sp.mV) * JPH::Vec3(scalp[tri.mIdx[2]]);
      rootError =
          std::max(rootError, double((JPH::Vec3(p) - expected).Length()));
      if (i)
        o << ',';
      o << '[' << p.x << ',' << p.y << ',' << p.z << ']';
    }
    auto matrix = [&](JPH::Mat44Arg m) {
      o << '[';
      for (unsigned j = 0; j < 4; j++) {
        JPH::Float4 v;
        m.GetColumn4(j).StoreFloat4(&v);
        if (j)
          o << ',';
        o << v.x << ',' << v.y << ',' << v.z << ',' << v.w;
      }
      o << ']';
    };
    o << "],\"hairWorldColumns\":";
    matrix(scene->hair()->GetWorldTransform());
    o << ",\"hullWorldColumns\":[";
    for (unsigned i = 0; i < scene->mAttachedBodies.size(); i++) {
      if (i)
        o << ',';
      matrix(physics->GetBodyInterface().GetWorldTransform(
          scene->mAttachedBodies[i].mBodyID));
    }
    // Explicit capture audit only: exact active convex planes in body-origin space.
    o << "],\"hullPlanesLocal\":[";
    for (unsigned i=0;i<scene->mAttachedBodies.size();++i) {
      if(i)o<<',';
      const auto shape=physics->GetBodyInterface().GetShape(scene->mAttachedBodies[i].mBodyID);
      const auto *convex=static_cast<const JPH::ConvexHullShape*>(shape.GetPtr());
      o<<'[';unsigned face=0;
      for(const auto &plane:convex->GetPlanes()) {
        if(face++)o<<',';
        JPH::Float4 p;plane.StoreFloat4(&p);
        p.w-=JPH::Vec3(p.x,p.y,p.z).Dot(convex->GetCenterOfMass());
        o<<'['<<p.x<<','<<p.y<<','<<p.z<<','<<p.w<<']';
      }
      o<<']';
    }
    for (unsigned i = 0; i < s.mSimVertices.size(); i++)
      finite &= std::isfinite(g[i].x) && std::isfinite(g[i].y) &&
                std::isfinite(g[i].z);
    for (unsigned i = 0; i < s.mRenderVertices.size(); i++)
      finite &= std::isfinite(r[i].x) && std::isfinite(r[i].y) &&
                std::isfinite(r[i].z);
    o << "],\"maxRootAttachmentErrorMeters\":" << rootError;
    if (scene->metaHuman) {
      // Explicit capture diagnostics only; normal rendering has no readback.
      const auto restHead=scene->mFaceAnimation[0][scene->mHeadJointIdx];
      o<<",\"earRegionRenderBindSamples\":[";
      bool first=true;
      for (unsigned i=0;i<s.mRenderVertices.size();++i) {
        auto p=restHead*JPH::Vec3(r[i]);
        if (std::abs(p.GetX())<.045f || std::abs(p.GetX())>.12f || p.GetY()<1.19f || p.GetY()>1.30f || p.GetZ()<-.045f || p.GetZ()>.10f)continue;
        if (!first)o<<',';first=false;
        o<<'['<<i<<','<<p.GetX()<<','<<p.GetY()<<','<<p.GetZ()<<']';
      }
      o<<']';
    }
    auto vec=[&](JPH::Vec3Arg p){o<<'['<<p.GetX()<<','<<p.GetY()<<','<<p.GetZ()<<']';};
    o<<",\"characterPositionWorld\":";vec(scene->locomotion.position);
    o<<",\"characterVelocityWorld\":";vec(scene->locomotion.velocity);
    o<<",\"characterYaw\":"<<scene->locomotion.yaw<<",\"walkPhase\":"<<scene->locomotion.phase;
    o<<",\"headPositionWorld\":";vec(scene->headWorldPosition);
    o<<",\"headVelocityWorld\":";vec(scene->headWorldVelocity);
    o<<",\"headAccelerationWorld\":";vec(scene->headWorldAcceleration);
    o<<",\"headPreviousPositionWorld\":";vec(scene->headPreviousPosition);
    o<<",\"headPreviousVelocityWorld\":";vec(scene->headPreviousVelocity);
    o<<",\"posedJointPositionsWorld\":[";
    for(unsigned i=0;i<scene->worldPose().size();++i){if(i)o<<',';vec(scene->worldPose()[i].GetTranslation());}
    o<<"],\"hullJointIds\":[";
    for(unsigned i=0;i<scene->mAttachedBodies.size();++i){if(i)o<<',';o<<scene->mAttachedBodies[i].mJointIdx;}
    o<<']';
    // Explicit capture-only validation; normal frames never read these arrays.
    {
      auto *hair=scene->hair();auto rb=hair->GetBodyStateCB()->CreateReadBackBuffer().Get();auto fb=hair->GetBodyFieldCB()->CreateReadBackBuffer().Get();
      computeQueue->ScheduleReadback(rb,hair->GetBodyStateCB());computeQueue->ScheduleReadback(fb,hair->GetBodyFieldCB());computeQueue->ExecuteAndWait();
      auto *state=rb->Map<JPH::Float4>(JPH::ComputeBuffer::EMode::Read);auto *field=fb->Map<JPH::uint32>(JPH::ComputeBuffer::EMode::Read);
      float skinError=0;auto toHair=hair->GetWorldTransform().InversedRotationTranslation();
      o<<",\"bodyPrimitiveInput\":[";for(unsigned k=0;k<scene->mBodyPrimitives.size();++k){if(k)o<<',';auto p=scene->mBodyPrimitives[k];o<<'['<<p.x<<','<<p.y<<','<<p.z<<','<<p.w<<']';}o<<']';
      o<<",\"bodyPrimitiveSourceIds\":[";for(unsigned k=0;k<scene->mBodyPrimitiveSources.size();++k){if(k)o<<',';o<<scene->mBodyPrimitiveSources[k];}o<<']';
      o<<",\"bodyPrimitives\":[";
      for(unsigned id=0;id<hair->GetBodyPrimitiveCount();++id){if(id)o<<',';o<<'[';
        for(unsigned k=0;k<4;++k){if(k)o<<',';auto p=state[id*4+k];o<<'['<<p.x<<','<<p.y<<','<<p.z<<','<<p.w<<']';}
        o<<']';
        for(unsigned e=0;e<2;++e){auto point=scene->mBodyPrimitives[id*6+e];auto js=scene->mBodyPrimitives[id*6+2+e*2];auto ws=scene->mBodyPrimitives[id*6+3+e*2];JPH::Vec3 expected=JPH::Vec3::sZero();
          for(unsigned k=0;k<4;++k)if((&ws.x)[k]>0){auto j=unsigned((&js.x)[k]);expected+=(&ws.x)[k]*(toHair*scene->worldPose()[j]*s.mScalpInverseBindPose[j]*JPH::Vec3(point.x,point.y,point.z));}
          skinError=std::max(skinError,(expected-JPH::Vec3(state[id*4+e].x,state[id*4+e].y,state[id*4+e].z)).Length());
        }
      }
      o<<"],\"bodyField\":[";for(unsigned k=0;k<384;++k){if(k)o<<',';o<<field[k];}o<<"],\"bodySkinMaxErrorMeters\":"<<skinError;
      rb->Unmap();fb->Unmap();if(!std::isfinite(skinError)||skinError>1e-5f)throw std::runtime_error("GPU body skin mismatch");
    }
    o<<",\"extraInertialForceApplied\":false";
    o
      << ",\"finite\":" << (finite ? "true" : "false") << '}';
    scene->hair()->UnlockReadBackBuffers();
    if (!finite || !o || rootError > 1e-5)
      throw std::runtime_error("Invalid explicit hair capture state");
  }
  JPH::Vec3 cameraMovement(float forward,float right) const {
    using namespace DirectX;
    auto f=XMLoadFloat3(&camera.target)-camera.eye();f=XMVectorSetY(f,0);
    if(XMVectorGetX(XMVector3LengthSq(f))<1e-8f)return JPH::Vec3::sZero();
    f=XMVector3Normalize(f);auto r=XMVector3Cross(XMVectorSet(0,1,0,0),f);
    XMFLOAT3 input;XMStoreFloat3(&input,f*forward+r*right);
    return JPH::Vec3(input.x,0,-input.z); // display LH -> Jolt RH, exactly once.
  }
  JPH::Vec3 movementInput() const {
    if(!playground)return JPH::Vec3::sZero();
    if(overrideInput)return overriddenInput;
    if(walkTest){
      if(steps>=30&&steps<120)return JPH::Vec3(0,0,-1);
      if(steps>=150&&steps<210)return JPH::Vec3(1,0,0);
      if(steps>=240&&steps<300)return JPH::Vec3(-.70710678f,0,-.70710678f);
      if(steps>=330&&steps<360)return JPH::Vec3(0,0,1);
      return JPH::Vec3::sZero();
    }
    // Bounded CLI replays must not depend on incidental physical key presses.
    if(stepLimit||frameLimit||controlCheck)return JPH::Vec3::sZero();
    const auto& io=ImGui::GetIO();
    if(io.WantCaptureKeyboard||io.WantTextInput||GetForegroundWindow()!=window)return JPH::Vec3::sZero();
    float forward=float(ImGui::IsKeyDown(ImGuiKey_W))-float(ImGui::IsKeyDown(ImGuiKey_S));
    float right=float(ImGui::IsKeyDown(ImGuiKey_D))-float(ImGui::IsKeyDown(ImGuiKey_A));
    return cameraMovement(forward,right);
  }
  void advanceStep() {
    auto t=std::chrono::steady_clock::now();
    scene->hairSimulationEnabled=hairSimulationEnabled;
    scene->animate=animation;scene->moveInput=movementInput();scene->Update(delta);
    auto result=physics->Update(delta,1,temp.get(),jobs.get());
    if(result!=JPH::EPhysicsUpdateError::None)throw std::runtime_error("Jolt physics update error");
    auto a=scene->headWorldAcceleration;
    if(!std::isfinite(a.GetX())||!std::isfinite(a.GetY())||!std::isfinite(a.GetZ()))throw std::runtime_error("Nonfinite head acceleration");
    lastUpdateMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
    ++steps;
    if(!profilePath.empty()) {
      if(!profileCsv.is_open()) {
        if(profilePath.has_parent_path())fs::create_directories(profilePath.parent_path());
        profileCsv.open(profilePath);profileDispatchCsv.open(profilePath.string()+".dispatch.csv");
        if(!profileCsv||!profileDispatchCsv)throw std::runtime_error("Cannot open profile CSV");
        profileCsv<<"step,phase,active_hulls,active_faces,hull_refits,cpu_hull_skin_ms,cpu_hull_refit_ms,cpu_hull_total_ms,cpu_broadphase_ms,cpu_hair_record_ms,cpu_submit_wait_ms,cpu_step_ms,gpu_total_ms,gpu_collision_search_ms,gpu_dispatch_sum_ms\n";
        profileDispatchCsv<<"step,kernel,groups_x,gpu_ms\n";
        profileCsv<<std::setprecision(10);profileDispatchCsv<<std::setprecision(10);
      }
      auto *q=static_cast<JPH::ComputeQueueDX12*>(computeQueue.GetPtr());
      const auto &sh=scene->mHairShaders;
      const std::pair<const JPH::ComputeShader*,const char*> labels[]={
        {sh.mTeleportCS.GetPtr(),"Teleport"},{sh.mApplyDeltaTransformCS.GetPtr(),"ApplyDeltaTransform"},
        {sh.mSkinVerticesCS.GetPtr(),"SkinVertices"},{sh.mSkinRootsCS.GetPtr(),"SkinRoots"},
        {sh.mApplyGlobalPoseCS.GetPtr(),"ApplyGlobalPose"},{sh.mCalculateCollisionPlanesCS.GetPtr(),"CollisionSearch"},
        {sh.mBodyPrepareCS.GetPtr(),"BodySkin"},{sh.mBodyFieldCS.GetPtr(),"BodyBitmask"},{sh.mBodyQueryCS.GetPtr(),"BodyNarrow"},
        {sh.mGridClearCS.GetPtr(),"GridClear"},{sh.mGridAccumulateCS.GetPtr(),"GridAccumulate"},
        {sh.mGridNormalizeCS.GetPtr(),"GridNormalize"},{sh.mIntegrateCS.GetPtr(),"Integrate"},
        {sh.mUpdateRootsCS.GetPtr(),"UpdateRoots"},{sh.mUpdateStrandsCS.GetPtr(),"UpdateStrands"},
        {sh.mUpdateVelocityCS.GetPtr(),"UpdateVelocity"},{sh.mUpdateVelocityIntegrateCS.GetPtr(),"UpdateVelocityIntegrate"},
        {sh.mCalculateRenderPositionsCS.GetPtr(),"RenderReconstruction"}};
      double collision=0,sum=0;
      if(profileGpu)for(const auto &entry:q->GetDispatchTimings()) {
        const char *name="Unknown";for(const auto &label:labels)if(entry.mShader==label.first){name=label.second;break;}
        profileDispatchCsv<<steps<<','<<name<<','<<entry.mGroupsX<<','<<entry.mMilliseconds<<'\n';
        sum+=entry.mMilliseconds;if(entry.mShader==sh.mCalculateCollisionPlanesCS.GetPtr())collision+=entry.mMilliseconds;
      }
      profileCsv<<steps<<','<<scene->locomotion.phase<<','<<scene->hair()->mLastCollisionHullCount<<','<<scene->hair()->mLastCollisionFaceCount<<','<<scene->profileHullRefits<<','
        <<scene->profileHullSkinMs<<','<<scene->profileHullRefitMs<<','<<scene->profileHullTotalMs<<','<<scene->hair()->mLastBroadPhaseCpuMs<<','
        <<scene->profileHairRecordMs<<','<<scene->profileHairWaitMs<<','<<lastUpdateMs<<','<<(profileGpu?q->GetTotalTimestampMilliseconds():-1)<<','<<(profileGpu?collision:-1)<<','<<(profileGpu?sum:-1)<<'\n';
    }
  }
  void drawGround(){
    if(!playground)return;
    struct Constants{DirectX::XMFLOAT4X4 vp,world;float color[4];DirectX::XMFLOAT4 eye;}c{};
    static_assert(sizeof(c)==160);
    DirectX::XMStoreFloat4x4(&c.vp,camera.matrix(float(width)/height));
    DirectX::XMStoreFloat4x4(&c.world,DirectX::XMMatrixIdentity());
    auto p=scene->locomotion.position;c.color[0]=p.GetX();c.color[2]=-p.GetZ();
    DirectX::XMStoreFloat4(&c.eye,camera.eye());
    commands->SetGraphicsRootSignature(root.Get());commands->SetGraphicsRoot32BitConstants(0,40,&c,0);
    commands->SetPipelineState(groundTriangles.Get());commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(6,1,0,0);
  }
  void render() {
    unsigned index = swap->GetCurrentBackBufferIndex();
    idle(); // Upstream compute queue + graphics exchange ownership only at
            // completed fences.
    graphicsTimings.Collect(fence->GetCompletedValue());
    auto now=std::chrono::steady_clock::now();
    double elapsed=std::chrono::duration<double>(now-previousFrameTime).count();previousFrameTime=now;
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    camera.aspect = float(width) / height;
    camera.update(GetForegroundWindow()==window && !minimized && !stepLimit && !frameLimit && !controlCheck && !walkCheck);
    ImGui::SetNextWindowPos({15, 15}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({365,playground?std::max(240.f,std::min(740.f,float(height)-30)):0.f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("HairLabDX12 - Moca",nullptr,ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextWrapped("Moca / imported 315-bone rig - Jolt Hair");
    ImGui::TextWrapped(hairstyle==2?"Imported Alembic / long wavy twin ponytail":"Project-authored groom / repaired scalp");
    ImGui::TextWrapped(animationClip==1?"Dance: Run plays the imported clip without WASD.":"Run / idle: use WASD to move, or select Wave Hip Hop Dance.");
    const char* animationNames[]={"Naruto Run / idle","Wave Hip Hop Dance"};
    if(ImGui::Combo("Animation clip",&animationClip,animationNames,2)){running=false;stepOnce=false;animation=true;resetScene();}
    if(ImGui::Button("Walking playground")){animationClip=0;animation=true;playground=true;view="quarter";running=false;resetScene();}
    ImGui::SameLine();
    if(ImGui::Button("Studio")){playground=false;view="portrait";running=false;resetScene();}
    if(playground){
      if(ImGui::Button("Quarter view")){view="quarter";configureCamera();}
      ImGui::SameLine();
      if(ImGui::Checkbox("Follow character",&followCharacter)&&followCharacter){camera.target=camera.initialTarget;}
      ImGui::TextWrapped("WASD: camera-relative walk | click scene after UI");
      auto v=scene->locomotion.velocity;auto a=scene->headWorldAcceleration;
      ImGui::Text("Speed %.2f m/s | yaw %.0f deg",v.Length(),scene->locomotion.yaw*180/JPH_PI);
      ImGui::Text("Clip: %s | %.3f s / %.0f fps",animationClip==1?"Wave Hip Hop Dance.fbx":"NarutoRun.fbx",scene->locomotion.clipDuration,scene->locomotion.clipFps);
      ImGui::Text("Head world a: %.2f %.2f %.2f m/s^2",a.GetX(),a.GetY(),a.GetZ());
      ImGui::TextWrapped("Jolt transform inertia; NO additional -a force");
      ImGui::Text("Clock: fixed 60 Hz | discarded %.3f s",discardedWallTime);
    }
    if(ImGui::Button("Beauty framing")){view="portrait";configureCamera();}
    ImGui::SameLine();
    if(ImGui::Button("Full hair framing")){view="hair";configureCamera();}
    if(ImGui::Button("Outfit framing")){view="bust";configureCamera();}
    if(scene->metaHuman) {
      const char* names[]={"Front","Left","Right","Back","Crown","Below front","Below left","Below right","Below back"};
      const char* presets[]={"audit-front","audit-left","audit-right","audit-back","audit-top","audit-low-front","audit-low-left","audit-low-right","audit-low-back"};
      int selected=-1;for(int i=0;i<9;++i)if(view==presets[i])selected=i;
      if(ImGui::Combo("Hair inspection",&selected,names,9)){view=presets[selected];configureCamera();}
    }
    if(scene->metaHuman) {
      if(ImGui::Button("Inspect left ear")){view="ear-left";configureCamera();}
      ImGui::SameLine();
      if(ImGui::Button("Inspect right ear")){view="ear-right";configureCamera();}
    }
    const char* styles[]={"Gloss black / blunt bob","Long straight (original)","MetaHuman / wavy twin ponytail"};
    if(ImGui::Combo("Hairstyle",&hairstyle,styles,3)){
      running=false;stepOnce=false;resetScene();
    }
    const char* guideLevels[]={"Full (current baseline)","1/2 guides","1/4 guides","1/8 guides"};
    int densitySelection=guideDensity;
    if(ImGui::Combo("Guide density",&densitySelection,guideLevels,4))changeGuideDensity(densitySelection);
    ImGui::Text("Guides %zu | Render %zu",scene->settings().mSimStrands.size(),scene->settings().mRenderStrands.size());
    ImGui::TextWrapped("Density change pauses and resets simulation; render strand count and material edits are preserved. Rebuilding may take a moment.");
    ImGui::TextWrapped("%s", adapterName.c_str());
    if (ImGui::Button(running ? "Pause" : "Run")) {
      running = !running;
      if(running && animationClip==1) animation=true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
      running = false;
      stepOnce = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      running = false;
      resetScene();
    }
    ImGui::Checkbox(animationClip==1?"Play dance animation":"Head turn + idle sway", &animation);
    if (ImGui::Checkbox("Hair physics", &hairSimulationEnabled)) {
      if (!running) stepOnce = true;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Off: hair follows the animated scalp without dynamics. Materials are preserved. Switching clears hair velocity; paused switches apply one step.");
    ImGui::Checkbox("Base hair / black scalp cap", &baseHair);
    ImGui::Checkbox("Render hair", &renderHair);
    ImGui::Checkbox("Guide hair", &guides);
    if (ImGui::Button("Render")) {
      renderHair = true;
      guides = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Guides")) {
      renderHair = false;
      guides = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Both")) {
      renderHair = guides = true;
    }
    ImGui::Checkbox("Head/scalp", &head);
    ImGui::Checkbox("Hide body (head + hair only)", &hideBody);
    ImGui::BeginDisabled(hideBody);
    ImGui::Checkbox("Denim jacket", &showJacket);
    ImGui::EndDisabled();
    ImGui::Checkbox("Collision hulls / GPU body capsules", &hulls);
    ImGui::Text("GPU body capsules: %u | XYZ bitmask: 32 bins/axis",scene->hair()->GetBodyPrimitiveCount());
    if (hulls)
      ImGui::Text("GPU body primitives: %u (neck, torso, shoulders, arms, hands)",scene->bodyHullCount);
    ImGui::Checkbox("Imported skeleton", &bones);
    auto &set = scene->mHairSettings;
    ImGui::Text("Guides %zu / render %zu", set->mSimStrands.size(),
                set->mRenderStrands.size());
    ImGui::Text("Step %u | simulated %.3f s", steps, steps * delta);
    ImGui::Text("Animation %u / %u | imported bones %zu", scene->frame(), scene->frameCount(), displayInverseBind.size());
    ImGui::Text("CPU update + GPU wait: %.3f ms", lastUpdateMs);
    ImGui::Text("FPS %.1f", ImGui::GetIO().Framerate);
    ImGui::TextWrapped("Upstream collision, LRA, damping, velocity grid and "
                       "render reconstruction enabled.");
    if (ImGui::CollapsingHeader("Jolt material settings")) {
        if (scene->metaHuman)
          ImGui::Combo("Hair region", &materialRegion, "Fitted crown\0Bangs\0Twin ponytails\0Long face framing\0Sideburns / ear wisps\0Cap / ear contact\0Ponytail / ear contact\0Soft nape wisps\0");
        else
          ImGui::Combo("Hair region", &materialRegion, "Side / back\0Bangs\0");
        auto &m = set->mMaterials[materialRegion];
        const auto beforeEdit = m;
        const int linkedRegion = scene->metaHuman ? hairlab::ContactMaterialPartner(materialRegion) : -1;
        if (linkedRegion >= 0)
          ImGui::TextWrapped("Edits include the matching contact strands. Contact radius and guide selection stay unchanged.");
        auto &stylingHold = stylingHolds[materialRegion];
      if (ImGui::SliderFloat("Styling spray", &stylingHold, 0, 1, "%.2f")) {
        // Region-specific styling presets. This changes existing Jolt
        // pose/skin targets, not the solver or the root attachment.
        float t=stylingHold<.5f?stylingHold*2:(stylingHold-.5f)*2;
        auto blend=[&](float a,float b,float c){return stylingHold<.5f?a+(b-a)*t:b+(c-b)*t;};
        m.mGlobalPose={.01f,blend(.0001f,.00025f,.0005f),0,blend(.5f,.65f,.8f)};
        m.mSkinGlobalPose={1,blend(.03f,.06f,.12f),0,.45f};
          if (materialRegion == 0 && scene->bob) {
            m.mGlobalPose.mMax=blend(.0002f,.0006f,.0012f);
            m.mSkinGlobalPose.mMax=blend(.05f,.1f,.18f);
          }
          if (materialRegion == 1 && (scene->bob || scene->metaHuman)) {
            m.mGlobalPose={.02f,blend(.0015f,.003f,.006f),0,.75f};
            m.mSkinGlobalPose={1,blend(.2f,.35f,.5f),0,.65f};
          }
      }
      if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Soft ends <-> stronger shape hold. Includes matching contact strands.\nAdvanced edits below override this preset; Reset restores defaults.");
      ImGui::SliderFloat("Pose retention root",&m.mGlobalPose.mMin,0,1.f,"%.4f",ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Pose retention tip",&m.mGlobalPose.mMax,0,1.f,"%.4f",ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Pose retention end fraction",&m.mGlobalPose.mMaxFraction,.01f,1,"%.2f");
      ImGui::SliderFloat("Skin follow tip",&m.mSkinGlobalPose.mMax,0,1,"%.2f");
      ImGui::Checkbox("Collision", &m.mEnableCollision);
      ImGui::Checkbox("Long range attachment", &m.mEnableLRA);
      ImGui::SeparatorText("Bend / twist stiffness (live)");
      float bendExponent = std::log10(m.mBendCompliance);
      if (ImGui::SliderFloat("Bend compliance log10", &bendExponent, -10.f, -3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
        m.mBendCompliance = std::pow(10.f, bendExponent);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Lower = stiffer; higher = softer. -7 means 1e-7. Applies to this region and matching contact strands on the next Run/Step. Reset restores scene defaults.");
      ImGui::Text("Bend compliance: %.3e", m.mBendCompliance);
      ImGui::Text("Length multipliers: %.1f / %.1f / %.1f / %.1f",
                  m.mBendComplianceMultiplier.x, m.mBendComplianceMultiplier.y,
                  m.mBendComplianceMultiplier.z, m.mBendComplianceMultiplier.w);
      if (!hairSimulationEnabled)
        ImGui::TextWrapped("Hair physics is off; stiffness affects motion when enabled.");
      else if (m.mGlobalPose.mMin == 1.f && m.mGlobalPose.mMax == 1.f)
        ImGui::TextWrapped("Full pose retention overrides visible bending in this region.");
        ImGui::SeparatorText("Velocity damping / averaging (live)");
        ImGui::SliderFloat("World air resistance", &m.mAirResistance, 0, 20, "%.2f /s", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Drag from particle velocity relative to stationary world air, including motion carried by the head. Zero disables. Separate from local linear damping; matching contact strands are linked.");
        ImGui::SliderFloat("Linear damping", &m.mLinearDamping, 0, 20, "%.2f /s", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Reduces linear velocity: exp(-damping * dt). Zero disables damping.");
        ImGui::SliderFloat("Angular damping", &m.mAngularDamping, 0, 20, "%.2f /s", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Reduces angular velocity. Higher values settle rotation faster.");
        ImGui::SliderFloat("Grid velocity factor root", &m.mGridVelocityFactor.mMin, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Grid velocity factor tip", &m.mGridVelocityFactor.mMax, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Blends towards nearby grid-average velocity. Jolt adjusts the factor for step duration.");
        ImGui::TextWrapped("Grid averaging aligns nearby strand velocities; damping reduces velocity. Edits apply on the next simulation update (Run or Step when paused).");
        if (ImGui::Button("Reset velocity settings")) {
          const bool longSide=hairstyle==1 && materialRegion==0;
          m.mAirResistance = longSide?hairlab::JoltScene::sLongAirResistance:hairlab::JoltScene::sAirResistance;
          m.mLinearDamping = materialRegion == 1 && (scene->bob || scene->metaHuman) ? hairlab::JoltScene::sFringeLinearDamping : hairlab::JoltScene::sLinearDamping;
          m.mAngularDamping = longSide?hairlab::JoltScene::sLongAngularDamping:materialRegion == 1 && (scene->bob || scene->metaHuman) ? hairlab::JoltScene::sFringeAngularDamping : hairlab::JoltScene::sAngularDamping;
          m.mGridVelocityFactor = hairlab::JoltScene::sGridVelocityFactor;
        }
      ImGui::SliderFloat("Friction", &m.mFriction, 0, 1);
      if (linkedRegion >= 0) {
        hairlab::ApplyLinkedMaterialEdits(beforeEdit, m, set->mMaterials[linkedRegion]);
        stylingHolds[linkedRegion] = stylingHold;
      }
      int iter = int(set->mNumIterationsPerSecond);
      if (ImGui::SliderInt("Iterations / second", &iter, 1, 960))
        set->mNumIterationsPerSecond = unsigned(iter);
    }
    if (ImGui::Button("Frame scene (F)"))
      camera.frame();
    ImGui::TextWrapped(
        "Arrow keys: pan up/down/left/right | Shift: faster\nRMB orbit / MMB pan / wheel / F frame / R camera reset\nClick the scene after using UI to restore keyboard camera control.");
    ImGui::End();
    ImGui::Render();
    if(rendered>0&&(running||stepOnce)&&(!stepLimit||steps<stepLimit)){
      if(stepOnce||stepLimit||controlCheck){advanceStep();fixedAccumulator=0;stepOnce=false;}
      else{
        discardedWallTime+=std::max(0.,elapsed-.10);fixedAccumulator+=std::min(.10,elapsed);
        unsigned count=0;
        while(fixedAccumulator>=double(delta)&&count<3){advanceStep();fixedAccumulator-=delta;++count;}
        if(fixedAccumulator>=delta){double discarded=std::floor(fixedAccumulator/delta)*delta;discardedWallTime+=discarded;fixedAccumulator-=discarded;}
      }
    }else fixedAccumulator=0;
    if(playground){
      auto p=scene->locomotion.position;DirectX::XMFLOAT3 anchor{p.GetX(),.68f,-p.GetZ()};
      if(followCharacter){camera.target.x+=anchor.x-followAnchor.x;camera.target.z+=anchor.z-followAnchor.z;}
      camera.initialTarget.x+=anchor.x-followAnchor.x;camera.initialTarget.z+=anchor.z-followAnchor.z;
      followAnchor=anchor;
    }
    if (hulls)
      updateHulls();
    if(bones){
      std::vector<JPH::Float3> vertices;
      const auto& pose=scene->worldPose();
      for(unsigned i=0;i<boneParents.size();++i)if(boneParents[i]>=0){
        JPH::Float3 a,b;pose[i].GetTranslation().StoreFloat3(&a);
        pose[boneParents[i]].GetTranslation().StoreFloat3(&b);
        vertices.push_back(a);vertices.push_back(b);
      }
      boneVertexCount=unsigned(vertices.size());boneVertices=upload(vertices.data(),vertices.size()*12);
    }
    check(allocators[index]->Reset(), "Allocator reset");
    check(commands->Reset(allocators[index].Get(), nullptr), "List reset");
    graphicsTimings.BeginFrame(commands.Get(),index,rendered,steps);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition = {
        targets[index].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET};
    commands->ResourceBarrier(1, &b);
    auto color = rtv(sceneColor ? 2 : index), dh = dsvHeap->GetCPUDescriptorHandleForHeapStart();
    float clear[] = {.985f, .985f, .985f, 1};
    commands->ClearRenderTargetView(color, clear, 0, nullptr);
    commands->ClearDepthStencilView(dh, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0,
                                    nullptr);
    commands->OMSetRenderTargets(1, &color, FALSE, &dh);
    D3D12_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    D3D12_RECT sc{0, 0, LONG(width), LONG(height)};
    commands->RSSetViewports(1, &vp);
    commands->RSSetScissorRects(1, &sc);
    graphicsTimings.Begin(commands.Get(),GraphicsTimings::Ground);
    drawGround();
    graphicsTimings.End(commands.Get(),GraphicsTimings::Ground);
    drawHair();
    if (sceneColor) {
      graphicsTimings.Begin(commands.Get(),GraphicsTimings::Resolve);
      // Resolve only the scene; ImGui and the existing PNG readback stay 1x.
      D3D12_RESOURCE_BARRIER resolve[2]{};
      resolve[0].Type = resolve[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      resolve[0].Transition = {sceneColor.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE};
      resolve[1].Transition = {targets[index].Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
          D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_DEST};
      commands->ResourceBarrier(2, resolve);
      commands->ResolveSubresource(targets[index].Get(), 0, sceneColor.Get(), 0,
                                  DXGI_FORMAT_R8G8B8A8_UNORM);
      for (auto &barrier : resolve)
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
      commands->ResourceBarrier(2, resolve);
      auto presentColor = rtv(index);
      commands->OMSetRenderTargets(1, &presentColor, FALSE, nullptr);
      graphicsTimings.End(commands.Get(),GraphicsTimings::Resolve);
    }
    bool shot = !captureDir.empty() && (rendered == 0 || steps % every == 0 ||
                                        (stepLimit && steps == stepLimit));
    if (shot && !captureUI)
      capture.record(device.Get(), commands.Get(), targets[index].Get());
    ID3D12DescriptorHeap *heaps[] = {srvHeap.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    graphicsTimings.Begin(commands.Get(),GraphicsTimings::UI);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commands.Get());
    graphicsTimings.End(commands.Get(),GraphicsTimings::UI);
    if (shot && captureUI)
      capture.record(device.Get(), commands.Get(), targets[index].Get());
    std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
    commands->ResourceBarrier(1, &b);
    graphicsTimings.EndFrame(commands.Get(),serial+1);
    check(commands->Close(), "List close");
    ID3D12CommandList *lists[] = {commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    check(queue->Signal(fence.Get(), ++serial), "Frame signal");
    frameFence[index] = serial;
    check(swap->Present(1, 0), "Present");
    rendered++;
    if (shot) {
      wait(serial);
      std::ostringstream n;
      n << (resetCapture ? "reset-" : "step-") << std::setw(4)
        << std::setfill('0') << steps;
      capture.save(captureDir / (n.str() + ".png"));
      saveState(captureDir / (n.str() + ".json"));
      write("Captured Moca GPU scene " + n.str());
    }
  }
  void run() {
    parse();
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.lpszClassName = L"HairLabDX12";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc))
      throw std::runtime_error("Window class");
    registered = true;
    RECT r{0, 0, LONG(width), LONG(height)};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    window = CreateWindowExW(0, wc.lpszClassName, L"HairLabDX12 - Moca",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr,
                             nullptr, instance, this);
    if (!window)
      throw std::runtime_error("Create window");
    initGraphics();
    initUi();
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    write("Jolt-only initialization OK");
    if(guideDensityCheck){
      if(captureDir.empty())throw std::runtime_error("--guide-density-check requires --capture-dir");
      running=false;animation=false;
      size_t baseline=0,renderCount=scene->settings().mRenderStrands.size();
      auto *initial=const_cast<JPH::HairSettings*>(&scene->settings());initial->mMaterials[2].mAirResistance=2.75f;initial->mMaterials[2].mLinearDamping=7.25f;
      for(int level:{0,1,2,3,0}){
        changeGuideDensity(level);render();
        const auto &set=scene->settings();size_t count=set.mSimStrands.size();
        if(level==0){if(baseline && count!=baseline)throw std::runtime_error("Guide restore count mismatch");baseline=count;}
        if(set.mRenderStrands.size()!=renderCount || std::abs(double(count)-double(baseline)/double(1u<<level))>set.mMaterials.size())throw std::runtime_error("Guide reduction count mismatch");
        if(set.mMaterials[2].mAirResistance!=2.75f||set.mMaterials[2].mLinearDamping!=7.25f)throw std::runtime_error("Density switch discarded material edits");
        auto dir=captureDir/("density-"+std::to_string(1u<<level));fs::create_directories(dir);saveState(dir/"initial.json");
        for(int i=0;i<2;++i){stepOnce=true;render();}saveState(dir/"stepped.json");
      }
      auditDebug();write("PASS runtime guide density: 1/2/4/8/1, render count, material edits, finite roots, GPU lifecycle");return;
    }
    if(bodyCheck){
      if(captureDir.empty())throw std::runtime_error("--body-check requires --capture-dir");
      fs::create_directories(captureDir);std::ofstream report(captureDir/"body-check.json");RunBodyPrimitiveChecks(compute,computeQueue,report);auditDebug();write("PASS GPU body primitive skin/field/narrow checks");return;
    }
    if(airCheck){
      if(captureDir.empty())throw std::runtime_error("--air-check requires --capture-dir");
      fs::create_directories(captureDir);std::ofstream report(captureDir/"air-check.json");
      RunAirResistanceChecks(compute,computeQueue,report);auditDebug();write("PASS world-air GPU analytic checks (12 cases)");return;
    }
    if(walkCheck){
      if(captureDir.empty())throw std::runtime_error("--walk-check requires --capture-dir");
      auto require=[](bool yes,const char* text){if(!yes)throw std::runtime_error(text);};
      running=false;animation=false;overrideInput=true;overriddenInput=JPH::Vec3::sZero();
      render();saveState(captureDir/"initial.json");
      render();require(steps==0,"Walking pause advanced");
      auto w=cameraMovement(1,0),d=cameraMovement(0,1);
      require(std::abs(w.Length()-1)<1e-5f&&std::abs(d.Length()-1)<1e-5f&&std::abs(w.Dot(d))<1e-5f,"WASD camera basis invalid");
      require((cameraMovement(-1,0)+w).Length()<1e-5f,"S did not reverse W");
      overriddenInput=w;
      for(unsigned i=0;i<60;++i){stepOnce=true;render();}
      require(scene->locomotion.position.Dot(w)>.7f,"Forward walking failed");
      require(scene->locomotion.velocity.Length()<=scene->locomotion.speed+.0001f,"Clip movement speed exceeded");
      saveState(captureDir/"forward.json");
      followCharacter=false;auto fixedCamera=camera.target;
      overriddenInput=d;
      for(unsigned i=0;i<20;++i){stepOnce=true;render();}
      require(camera.target.x==fixedCamera.x&&camera.target.y==fixedCamera.y&&camera.target.z==fixedCamera.z,"Free camera moved with character");
      overriddenInput=JPH::Vec3::sZero();
      const unsigned brakeSteps=unsigned(std::ceil(scene->locomotion.speed/scene->locomotion.braking/delta))+4;
      for(unsigned i=0;i<brakeSteps;++i){stepOnce=true;render();}
      require(scene->locomotion.velocity.Length()<1e-6f,"Walking release did not brake");
      unsigned stopped=steps;render();render();require(steps==stopped,"Paused walking advanced");
      saveState(captureDir/"stopped.json");
      resetScene();followCharacter=true;render();saveState(captureDir/"reset.json");
      require(scene->locomotion.position.Length()==0&&scene->headWorldAcceleration.Length()==0,"Walking reset history not cleared");
      stepOnce=true;render();require(scene->headWorldAcceleration.Length()<1e-4f,"Stationary restart acceleration spike");
      playground=false;view="portrait";resetScene();render();saveState(captureDir/"studio.json");
      require(!scene->walkingEnabled&&scene->headWorldAcceleration.Length()==0,"Studio transition failed");
      playground=true;view="quarter";resetScene();render();saveState(captureDir/"returned.json");
      auditDebug();write("PASS walking controller: camera-relative axes, forward, brake, pause, free camera, reset history, mode switches");return;
    }
    if (controlCheck) {
      if (captureDir.empty())
        throw std::runtime_error("--control-check requires --capture-dir");
      if (scene->metaHuman) {
        for (int source : {0, 5, 2, 6}) {
          const int partner = hairlab::ContactMaterialPartner(source);
          auto before = scene->mHairSettings->mMaterials[source];
          auto after = before;
          auto target = scene->mHairSettings->mMaterials[partner];
          const auto preserved = target;
          after.mGlobalPose = {.12f, .08f, .05f, .9f};
          after.mSkinGlobalPose = {.9f, .3f, .1f, .8f};
          after.mGridVelocityFactor = {.4f, .2f, .05f, .95f};
          after.mLinearDamping = 11;
          after.mAngularDamping = 12;
          after.mFriction = .7f;
          after.mBendCompliance = 3.e-6f;
          after.mAirResistance = 2.5f;
          after.mEnableCollision = !before.mEnableCollision;
          after.mEnableLRA = !before.mEnableLRA;
          hairlab::ApplyLinkedMaterialEdits(before, after, target);
          auto gradientMatches = [](const auto &a, const auto &b) {
            return a.mMin==b.mMin && a.mMax==b.mMax && a.mMinFraction==b.mMinFraction && a.mMaxFraction==b.mMaxFraction;
          };
          if (!gradientMatches(after.mGlobalPose,target.mGlobalPose) ||
              !gradientMatches(after.mSkinGlobalPose,target.mSkinGlobalPose) ||
              !gradientMatches(after.mGridVelocityFactor,target.mGridVelocityFactor) ||
              target.mLinearDamping!=11 || target.mAngularDamping!=12 || target.mFriction!=.7f ||
              target.mBendCompliance!=after.mBendCompliance ||
              target.mAirResistance!=after.mAirResistance ||
              target.mEnableCollision!=after.mEnableCollision || target.mEnableLRA!=after.mEnableLRA ||
              !gradientMatches(target.mHairRadius,preserved.mHairRadius) ||
              target.mSimulationStrandsFraction!=preserved.mSimulationStrandsFraction)
            throw std::runtime_error("Linked contact material edit failed");
          // A no-op edit must preserve deliberate differences in the partner.
          auto noEdit = preserved;
          hairlab::ApplyLinkedMaterialEdits(before,before,noEdit);
          if (!gradientMatches(noEdit.mGlobalPose,preserved.mGlobalPose) || noEdit.mBendCompliance!=preserved.mBendCompliance || noEdit.mAirResistance!=preserved.mAirResistance)
            throw std::runtime_error("Linked no-op changed contact material");
        }
        write("PASS linked material edits: crown/contact and ponytail/contact, both directions; radius/guide fraction preserved");
      }
      running = false;
      render();
      saveState(captureDir / "idle-before.json");
      render();
      render();
      saveState(captureDir / "idle-after.json");
      if (steps != 0)
        throw std::runtime_error("Pause advanced");
      stepOnce = true;
      render();
      saveState(captureDir / "single-step.json");
      if (steps != 1)
        throw std::runtime_error("Step failed");
      running = true;
      render();
      running = false;
      render();
      if (steps != 2)
        throw std::runtime_error("Run/Pause failed");
      const auto savedMaterials = scene->mHairSettings->mMaterials;
      const auto frameBeforePhysicsOff = scene->frame();
      const bool savedAnimation = animation;
      animation = true;
      hairSimulationEnabled = false;
      stepOnce = true; render();
      stepOnce = true; render();
      saveState(captureDir / "hair-physics-off.json");
      if (scene->frame() == frameBeforePhysicsOff || scene->hairSimulationEnabled)
        throw std::runtime_error("Hair physics off stopped animation");
      for (size_t i=0; i<savedMaterials.size(); ++i) {
        const auto &a=savedMaterials[i]; const auto &b=scene->mHairSettings->mMaterials[i];
        if (a.mGlobalPose.mMin!=b.mGlobalPose.mMin || a.mGlobalPose.mMax!=b.mGlobalPose.mMax ||
            a.mSkinGlobalPose.mMax!=b.mSkinGlobalPose.mMax || a.mEnableCollision!=b.mEnableCollision)
          throw std::runtime_error("Hair physics toggle overwrote materials");
      }
      hairSimulationEnabled = true;
      stepOnce = true; render();
      saveState(captureDir / "hair-physics-on.json");
      animation = savedAnimation;
      write("PASS hair physics toggle: animated skin-only updates, material preservation, resume");
      if(scene->metaHuman) {
        auto before=scene->mHairSettings->mMaterials[2];
        scene->mHairSettings->mMaterials[2].mAirResistance=4.f;
        hairlab::ApplyLinkedMaterialEdits(before,scene->mHairSettings->mMaterials[2],scene->mHairSettings->mMaterials[6]);
        scene->mHairSettings->mMaterials[3].mAirResistance=2.f;
        stepOnce=true;render();saveState(captureDir/"air-groups-live.json");
        for(unsigned i=0;i<8;++i) {
          float expected=i==2||i==6?4.f:i==3?2.f:hairlab::JoltScene::sAirResistance;
          if(scene->mHairSettings->mMaterials[i].mAirResistance!=expected)throw std::runtime_error("Air group isolation failed");
        }
        write("PASS air group live update: ponytail/contact linked, side bangs independent, other groups preserved");
      }
      saveState(captureDir / "camera-before.json");
      camera.yaw += .15f;
      camera.distance *= .9f;
      render();
      saveState(captureDir / "camera-after.json");
      const int originalStyle=hairstyle;
      hairstyle=(hairstyle+1)%3;
      resetScene();render();saveState(captureDir / "style-switched.json");
      hairstyle=originalStyle;
      resetScene();
      running = false;
      rendered = 0;
      resetCapture = true;
      render();
      saveState(captureDir / "control-reset.json");
      auditDebug();
      write("PASS Moca controller: pause / single-step / run-pause / reset / "
            "camera API");
      return;
    }
    bool done = false;
    while (!done && (!stepLimit || steps < stepLimit) &&
           (!frameLimit || rendered < frameLimit)) {
      MSG m{};
      while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessage(&m);
        done |= m.message == WM_QUIT;
      }
      if (done)
        break;
      if (minimized) {
        running = false;
        if (stepLimit || frameLimit)
          throw std::runtime_error("Bounded run minimized");
        WaitMessage();
        continue;
      }
      if (pendingWidth && pendingHeight) {
        if (width != pendingWidth || height != pendingHeight) {
          idle();
          for (auto &t : targets)
            t.Reset();
          depth.Reset();
          sceneColor.Reset();
          width = pendingWidth;
          height = pendingHeight;
          check(swap->ResizeBuffers(2, width, height,
                                    DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                "Resize");
          createTargets();
        }
        pendingWidth = pendingHeight = 0;
      }
      render();
    }
    idle();
    graphicsTimings.Collect(fence->GetCompletedValue());
    const unsigned completedSteps=steps;const double completedDiscarded=discardedWallTime;
    if (resetCheck) {
      resetScene();
      running = false;
      rendered = 0;
      resetCapture = true;
      render();
      write("Reset completed: Moca bind pose, frame 0");
    }
    graphicsTimings.Collect(fence->GetCompletedValue());
    auditDebug();
    write("Clock: fixed_dt="+std::to_string(delta)+", simulated_s="+std::to_string(completedSteps*delta)+", discarded_wall_s="+std::to_string(completedDiscarded));
    write("Clean exit; Jolt steps=" + std::to_string(steps) +
          ", rendered=" + std::to_string(rendered));
  }
  ~Application() {
    try {
      idle();
      if (computeQueue)
        computeQueue->Wait();
    } catch (...) {
    }
    scene.reset();
    physics.reset();
    computeQueue = nullptr;
    if (compute)
      compute->Shutdown();
    compute = nullptr;
    jobs.reset();
    temp.reset();
    if (joltReady) {
      JPH::UnregisterTypes();
      delete JPH::Factory::sInstance;
      JPH::Factory::sInstance = nullptr;
    }
    if (dx12Ready)
      ImGui_ImplDX12_Shutdown();
    if (win32Ready)
      ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext())
      ImGui::DestroyContext();
    if (fenceEvent)
      CloseHandle(fenceEvent);
    if (window)
      DestroyWindow(window);
    if (registered)
      UnregisterClassW(L"HairLabDX12", instance);
  }
};
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  Application app;
  try {
    app.run();
    return 0;
  } catch (const std::exception &e) {
    app.write(std::string("FAILURE: ") + e.what());
    try {
      app.auditDebug();
    } catch (const std::exception &d) {
      app.write(d.what());
    }
    return 1;
  }
}

