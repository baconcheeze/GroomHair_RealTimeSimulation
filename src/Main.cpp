#include <windows.h>
#include <shellapi.h>
#include <wrl/client.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <array>
#include <vector>
#include <cstring>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "OrbitCamera.h"
#include "DebugScene.h"
#include "AppOptions.h"
#include "RestData.h"
#include "StaticPreview.h"
#include "SceneCapture.h"
#include "OrientationSession.h"
#include "WindowSession.h"
#include "DynamicsSession.h"
#include "NativeReferenceView.h"
#include <iomanip>
#include <functional>
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
static void check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) { std::ostringstream s; s << operation << " failed: 0x" << std::hex << static_cast<unsigned>(hr); throw std::runtime_error(s.str()); }
}
static std::string utf8(const wchar_t* s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s,-1,nullptr,0,nullptr,nullptr);
    std::string out(n,'\0'); WideCharToMultiByte(CP_UTF8,0,s,-1,out.data(),n,nullptr,nullptr); out.pop_back(); return out;
}
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND,UINT,WPARAM,LPARAM);
struct Application {
    HWND window{}; HINSTANCE instance=GetModuleHandleW(nullptr); bool registered{}, minimized{};
    UINT width=1280,height=800,pendingWidth{},pendingHeight{};
    bool win32Ready{},dx12Ready{},demo{},warp{}; unsigned frameLimit{},rendered{};
    std::ofstream log; std::string adapterName;
    ComPtr<IDXGIFactory6> factory; ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device; ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGISwapChain3> swap;
    ComPtr<ID3D12DescriptorHeap> rtvHeap,dsvHeap,srvHeap;
    std::array<ComPtr<ID3D12Resource>,2> targets;
    ComPtr<ID3D12Resource> depth,vertexBuffer;
    std::array<ComPtr<ID3D12CommandAllocator>,2> allocators;
    std::array<UINT64,2> frameFence{};
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence; UINT64 serial{}; HANDLE fenceEvent{};
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> lines,triangles;
    UINT rtvStride{},srvStride{}; std::vector<unsigned> freeSrv;
    D3D12_VERTEX_BUFFER_VIEW vertexView{};
    OrbitCamera camera; DebugScene scene;
    AppOptions options; RestData rest; StaticPreview preview; SceneCapture capture;
    OrientationSession orientation;
    WindowSession windowSession;
    NativeReferenceView reference;bool referenceStarted=false,referenceSequenceDone=false,referenceWaitingReset=false;uint64_t referenceNextCapture=0;bool referenceBackendSet=false;
    DynamicsSession dynamics;uint64_t dynamicsLoggedGeneration=0;
    std::array<UploadList,2> orientationUploads;
    bool hairMode=true,restLoaded=false,initialValidated=false,finalValidated=false;
    fs::path executableDirectory() const {
        wchar_t path[32768]; DWORD n=GetModuleFileNameW(nullptr,path,32768);
        if(!n || n==32768) throw std::runtime_error("Executable path unavailable");
        return fs::path(path).parent_path();
    }
    void immediate(const std::function<void(ID3D12GraphicsCommandList*)>& record) {
        idle(); check(allocators[0]->Reset(),"Reset startup allocator"); check(commands->Reset(allocators[0].Get(),nullptr),"Reset startup list"); record(commands.Get()); check(commands->Close(),"Close startup list");
        ID3D12CommandList* lists[]{commands.Get()}; queue->ExecuteCommandLists(1,lists); idle();
    }
    void validateRest() {
        bool moving=windowSession.initialized&&(windowSession.mode==2||windowSession.mode==3);
        immediate([&](auto* list){if(orientation.ready)orientation.state.position.transition(list,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);preview.recordValidation(device.Get(),list,moving);}); preview.finishValidation();
        write(moving?"PASS immutable rest / render positions / connectivity (window guide motion allowed)":"PASS GPU exact byte readback: Guide/Render RestPosition, CurrentPosition, SegmentStarts");
    }
    Bounds hairBounds() const {
        Bounds b=rest.render.bounds; const auto& head=preview.original?rest.originalHead:rest.selectedHead;
        b.add(rest.guide.bounds.min); b.add(rest.guide.bounds.max); b.add(head.bounds.min); b.add(head.bounds.max); return b;
    }
    void selectScene(bool hair) {
        if(hair && !restLoaded) throw std::runtime_error("Hair scene assets not loaded");
        hairMode=hair;if(!hair)reference.run(false);if(!hair&&dynamics.initialized){dynamics.running=false;dynamics.suspend();} camera=OrbitCamera{};
        scene.mesh=!hair; scene.grid=scene.axes=!hair;
        if(hair) { auto b=hairBounds(); camera.setHair(b.center(),b.radius(),rest.cameraBasis,static_cast<float>(width)/height); }
    }
    void initializeHair() {
        rest.load(executableDirectory()/"assets"); restLoaded=true;
        immediate([&](auto* list){preview.initialize(device.Get(),list,rest,executableDirectory()/"shaders");}); preview.releaseUploads();
        preview.guides=options.draw=="guides" || options.draw=="both";
        preview.renderHair=options.draw=="render" || options.draw=="both";
        preview.hulls=options.draw=="hulls"; preview.original=options.originalHead;
        write("Dataset: "+rest.dataset+"\nGuide: "+rest.guide.diagnostics()+"\nRender: "+rest.render.diagnostics());
        orientation.initialize(device.Get(),[&](const auto& record){immediate(record);},rest.guide,preview.guideCurrentPosition(),preview.guideRestPosition(),preview.guideSegmentStarts(),executableDirectory()/"shaders/orientation");
        write("Guide initialized: "+orientation.cpu.diagnostics.json());
        orientation.solver.settings.participating=options.participating;orientation.solver.settings.pinRoot=options.pinRoot;orientation.solver.settings.showFrames=options.materialFrames;orientation.solver.settings.frameStride=options.frameStride;
        orientation.solver.settings.autoSolve=options.autoOrientation;
        windowSession.initialize(device.Get(),[&](const auto& record){immediate(record);},orientation.state,executableDirectory()/"shaders/window");
        if(orientation.state.position.resource.Get()!=preview.guideCurrentPosition())throw std::runtime_error("Guide current-position ownership mismatch");
        write("Window solver and guide renderer share the same UAV-capable CurrentPosition allocation");
        windowSession.mode=options.solverMode=="reference"?4:options.solverMode=="dynamics"?3:options.solverMode=="window"?2:options.solverMode=="orientation"?1:0;
        windowSession.solver.settings={options.participating,options.windowH,1,1e-5f,options.windowPinX,options.windowPinQ};
        if(windowSession.mode==2){orientation.cancelQueued();windowSession.autoRelax=options.windowAuto;UploadList keep;immediate([&](auto* list){if(!options.windowTarget.empty())windowSession.solver.prepare(device.Get(),list,windowmath::targets(orientation.cpu,options.windowTarget),options.windowTarget,keep);if(!options.windowPerturb.empty())windowSession.solver.perturb(list,options.windowPerturb=="position"?1:options.windowPerturb=="orientation"?2:3);if(options.windowSweeps)windowSession.solver.run(list,options.windowSweeps);if(options.windowReset)windowSession.solver.resetCurrent(device.Get(),list,keep);});}
        dynamics.initialize(device.Get(),[&](const auto& record){immediate(record);},orientation.state,windowSession.solver,executableDirectory()/"shaders/dynamics",rest.worldToHairRotation);
        dynamics.deterministic=options.simSpecified||!options.test.empty();
        dynamics.solver.settings={options.participating,options.simH,1,1e-5f,options.windowPinX,options.windowPinQ};
        dynamics.solver.initializer=options.simInitializer;dynamics.solver.diagnosticSnapshots=options.simDiagnostics;dynamics.solver.sweeps=options.sweepsPerStep;dynamics.setWorldGravity({options.gravity[0],options.gravity[1],options.gravity[2]});
        if(windowSession.mode==3){orientation.cancelQueued();UploadList keep;immediate([&](auto* list){dynamics.solver.reset(device.Get(),list,keep);if(options.simVelocity!=std::array<float,3>{0,0,0})dynamics.solver.kick(list,{options.simVelocity[0],options.simVelocity[1],options.simVelocity[2]});if(options.simSteps)dynamics.solver.step(list,options.simSteps);if(options.simReset)dynamics.solver.reset(device.Get(),list,keep);});write("Dynamics gravity solver frame: ["+std::to_string(dynamics.solver.gravity.x)+","+std::to_string(dynamics.solver.gravity.y)+","+std::to_string(dynamics.solver.gravity.z)+"] m/s^2; fixed frame-0 neck rotation from staged pose (world to hair vector, no translation)");}
        if(!options.orientationPerturb.empty()||options.orientationSweeps||options.resetAfter)orientation.startup(device.Get(),[&](const auto& record){immediate(record);},options.orientationPerturb,options.orientationSweeps,options.resetAfter);
        if(!options.test.empty()) {
            OrientationTestContext context{device.Get(),[&](const auto& record){immediate(record);},executableDirectory()/"shaders/orientation",options.report,warp};
            orientation.state.verifyImmutable(device.Get(),context.submit,true);
            if(options.test=="orientation-local")testOrientationLocal(context);
            if(options.test=="orientation-groom")testOrientationGroom(orientation.state,orientation.solver,context);
            if(options.test=="dynamics-local"){testDynamicsLocal(context);testDynamicsModeTransitions(dynamics,windowSession,orientation,context);}
            if(options.test=="dynamics-groom")testDynamicsGroom(orientation.state,windowSession.solver,dynamics.solver,context,queue.Get(),worldVectorToHair(Float3{0,-9.81f,0},rest.worldToHairRotation));
            if(options.test=="position-local")testPositionLocal(context);
            if(options.test=="window-solver"){testWindowSolver(context);testWindowModeTransitions(windowSession,orientation,context);}
            if(options.test=="window-groom")testWindowGroom(orientation.state,windowSession.solver,context);
            write("Explicit test initialized/completed: "+options.test);
        }
        if(windowSession.mode==4) initializeReference();
        if(options.validate) { validateRest(); initialValidated=true; }
    }
    void initializeReference(){reference.initialize(orientation.cpu,executableDirectory()/"assets/reference-demo",rest,options.referenceGuides,options.referenceTip);reference.gpuDevice=device.Get();reference.gpuSubmit=[&](const auto& record){immediate(record);};check(queue->GetTimestampFrequency(&reference.gpuFrequency),"GPU timestamp frequency");if(!referenceBackendSet){referenceBackendSet=true;if(options.referenceBackend=="gpu"){if(warp)throw std::runtime_error("GPU full-strand demo requires hardware, not WARP");reference.gpu.refinements=options.referenceRefinements;reference.setBackend(true);}}write(reference.gpuBackend?"GPU Full-Strand: authoritative GPU binary64; device physical assembly/nonlinear/dynamics; c="+std::to_string(reference.gpu.refinements):"CPU Native Reference: authoritative CPU double; NativeExtended five corrections; fixed h; render-only float bridge");}
    bool referenceReady()const{return reference.current&&reference.current->generation==reference.generation();}
    void finishReferenceCapture(unsigned index){
      wait(frameFence[index]);reference.captureState();auto s=reference.current;std::ostringstream name;if(referenceWaitingReset)name<<"reset";else name<<"step-"<<std::setw(4)<<std::setfill('0')<<s->step;
      capture.save(options.referenceCaptureDir/(name.str()+".png"));NativeReferenceDemo::save(options.referenceCaptureDir/(name.str()+".json"),*s);write("Reference captured "+name.str()+"; active="+std::to_string(s->x.size())+"; backend="+std::string(reference.gpuBackend?"GPU":"CPU")+"; measured work ms="+std::to_string(s->solveMs));
      if(referenceWaitingReset){if(!(s->step==0&&s->maxDisplacement==0&&s->kicks==0))throw std::runtime_error("Reference reset failed");referenceSequenceDone=true;return;}
      if(s->step==options.referenceSteps){if(!options.report.empty())NativeReferenceDemo::save(options.report,*s);if(options.referenceResetCheck){reference.reset(reference.count);referenceWaitingReset=true;}else referenceSequenceDone=true;return;}
      if(s->step==0&&options.referenceKick!=std::array<float,3>{0,0,0})reference.kick(worldVectorToHair({options.referenceKick[0],options.referenceKick[1],options.referenceKick[2]},rest.worldToHairRotation));
      referenceNextCapture=std::min(uint64_t(options.referenceSteps),s->step+options.referenceEvery);reference.steps(unsigned(referenceNextCapture-s->step));
    }
    void hairPanel() {
        if(!restLoaded) return;
        if(windowSession.mode==4){ImGui::TextWrapped("Dataset: %s",rest.dataset.c_str());ImGui::Checkbox("Draw Head",&preview.head);ImGui::SameLine();ImGui::Checkbox("Draw Collision Hulls",&preview.hulls);return;}
        ImGui::Text("Dataset: %s",rest.dataset.c_str());
        ImGui::Text("Guides: %zu strands / %zu vertices / %zu segments",rest.guide.sourceIds.size(),rest.guide.positions.size(),rest.guide.segments.size());
        ImGui::Text("Render: %zu strands / %zu vertices / %zu segments",rest.render.sourceIds.size(),rest.render.positions.size(),rest.render.segments.size());
        auto& m=preview.original?rest.originalHead:rest.selectedHead;
        ImGui::Text("Head: %zu vertices / %zu triangles",m.vertices.size(),m.indices.size()/3); ImGui::Text("Hulls: %zu (original, not refitted)",rest.hulls.size());
        auto b=hairBounds(); ImGui::Text("Bounds min: %.4f %.4f %.4f",b.min.x,b.min.y,b.min.z); ImGui::Text("Bounds max: %.4f %.4f %.4f",b.max.x,b.max.y,b.max.z);
        ImGui::TextWrapped("Meters, LH Unity neck-local; identity object pose. Camera uses inverse converted frame-0 neck axes.");
        ImGui::Checkbox("Draw Guide Hair",&preview.guides); ImGui::Checkbox("Draw Render Hair",&preview.renderHair); ImGui::Checkbox("Draw Head",&preview.head); ImGui::Checkbox("Draw Collision Hulls",&preview.hulls); if(ImGui::Checkbox("Original Jolt scalp (frame-0)",&preview.original)) selectScene(true);
    }
    void write(const std::string& s) { if(log) { log << s << '\n'; log.flush(); } OutputDebugStringA((s+"\n").c_str()); }
    void wait(UINT64 value) {
        if(value && fence->GetCompletedValue()<value) {
            check(fence->SetEventOnCompletion(value,fenceEvent),"SetEventOnCompletion");
            DWORD result=WaitForSingleObject(fenceEvent,15000);
            if(result!=WAIT_OBJECT_0) throw std::runtime_error("GPU fence wait failed or timed out");
        }
        if(fence->GetCompletedValue()==UINT64_MAX) check(device->GetDeviceRemovedReason(),"Device removed");
    }
    void idle() { if(queue && fence && fenceEvent) { check(queue->Signal(fence.Get(),++serial),"Signal idle"); wait(serial); } }
    void auditDebugMessages() {
#ifdef _DEBUG
        ComPtr<ID3D12InfoQueue> info;
        if(SUCCEEDED(device.As(&info))) {
            bool failed=false;
            for(UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
                SIZE_T size{}; check(info->GetMessage(i,nullptr,&size),"Get debug message size");
                std::vector<unsigned char> storage(size); auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
                check(info->GetMessage(i,message,&size),"Get debug message");
                if(message->Severity<=D3D12_MESSAGE_SEVERITY_WARNING) write(std::string("D3D12: ")+message->pDescription);
                if(message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) failed=true;
            }
            if(failed) throw std::runtime_error("D3D12 debug layer reported errors");
            write("D3D12 debug audit: no errors");
        }
#endif
    }
    ~Application() {
        reference.simulation.shutdown();
        try { idle(); } catch(const std::exception& e) { write(e.what()); }
        if(dx12Ready) ImGui_ImplDX12_Shutdown();
        if(win32Ready) ImGui_ImplWin32_Shutdown();
        if(ImGui::GetCurrentContext()) ImGui::DestroyContext();
        if(fenceEvent) CloseHandle(fenceEvent);
        if(window) DestroyWindow(window);
        if(registered) UnregisterClassW(L"HairLabDX12",instance);
    }
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        auto* app=reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
        if(msg==WM_NCCREATE) { app=static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
        if(ImGui::GetCurrentContext() && ImGui_ImplWin32_WndProcHandler(hwnd,msg,wp,lp)) return 1;
        if(app && msg==WM_SIZE) {
            app->minimized=wp==SIZE_MINIMIZED;
            if(!app->minimized) { app->pendingWidth=LOWORD(lp); app->pendingHeight=HIWORD(lp); }
            return 0;
        }
        if(msg==WM_SYSCOMMAND && (wp&0xfff0)==SC_KEYMENU) return 0;
        if(msg==WM_CLOSE) { PostQuitMessage(0); return 0; }
        if(msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE rtv(unsigned index) { auto h=rtvHeap->GetCPUDescriptorHandleForHeapStart(); h.ptr+=index*rtvStride; return h; }
    void createTargets() {
        for(unsigned i=0;i<2;++i) { check(swap->GetBuffer(i,IID_PPV_ARGS(&targets[i])),"GetBuffer"); device->CreateRenderTargetView(targets[i].Get(),nullptr,rtv(i)); }
        D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width=width; desc.Height=height; desc.DepthOrArraySize=1; desc.MipLevels=1; desc.Format=DXGI_FORMAT_D32_FLOAT; desc.SampleDesc.Count=1; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE clear{}; clear.Format=desc.Format; clear.DepthStencil.Depth=1;
        check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Create depth");
        device->CreateDepthStencilView(depth.Get(),nullptr,dsvHeap->GetCPUDescriptorHandleForHeapStart());
    }
    void initGraphics() {
        UINT flags=0;
#ifdef _DEBUG
        ComPtr<ID3D12Debug> debug;
        if(SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) { debug->EnableDebugLayer(); flags=DXGI_CREATE_FACTORY_DEBUG; write("D3D12 debug layer enabled"); }
        else write("D3D12 debug layer unavailable; continuing without it");
#endif
        HRESULT factoryResult=CreateDXGIFactory2(flags,IID_PPV_ARGS(&factory));
        if(FAILED(factoryResult) && flags) { write("DXGI debug factory unavailable; using ordinary factory"); factoryResult=CreateDXGIFactory2(0,IID_PPV_ARGS(&factory)); }
        check(factoryResult,"CreateDXGIFactory2");
        if(warp) check(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)),"EnumWarpAdapter");
        else {
            for(UINT i=0;;++i) {
                ComPtr<IDXGIAdapter1> candidate;
                HRESULT hr=factory->EnumAdapterByGpuPreference(i,DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,IID_PPV_ARGS(&candidate));
                if(hr==DXGI_ERROR_NOT_FOUND) break; check(hr,"EnumAdapterByGpuPreference");
                DXGI_ADAPTER_DESC1 desc{}; check(candidate->GetDesc1(&desc),"GetDesc1");
                if(!(desc.Flags&DXGI_ADAPTER_FLAG_SOFTWARE) && SUCCEEDED(D3D12CreateDevice(candidate.Get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),nullptr))) { adapter=candidate; break; }
            }
            if(!adapter) throw std::runtime_error("No DX12 hardware adapter. Use --warp explicitly for software rendering.");
        }
        DXGI_ADAPTER_DESC1 desc{}; check(adapter->GetDesc1(&desc),"GetDesc1"); adapterName=utf8(desc.Description);
        write("Adapter: "+adapterName+(warp?" [WARP SOFTWARE]":" [HARDWARE]"));
        check(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)),"D3D12CreateDevice");
        D3D12_COMMAND_QUEUE_DESC q{}; q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue)),"CreateCommandQueue");
        DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=width; sd.Height=height; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM; sd.SampleDesc.Count=1; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2; sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> initial; check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&initial),"CreateSwapChainForHwnd"); check(initial.As(&swap),"SwapChain3");
        check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"MakeWindowAssociation");
        auto heap=[&](D3D12_DESCRIPTOR_HEAP_TYPE type,UINT count,bool visible,ComPtr<ID3D12DescriptorHeap>& out) { D3D12_DESCRIPTOR_HEAP_DESC h{}; h.Type=type; h.NumDescriptors=count; h.Flags=visible?D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE:D3D12_DESCRIPTOR_HEAP_FLAG_NONE; check(device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&out)),"CreateDescriptorHeap"); };
        heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,2,false,rtvHeap); heap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,1,false,dsvHeap); heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,64,true,srvHeap);
        rtvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); srvStride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for(unsigned i=64;i>0;--i) freeSrv.push_back(i-1);
        for(auto& a:allocators) check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)),"CreateCommandAllocator");
        check(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[0].Get(),nullptr,IID_PPV_ARGS(&commands)),"CreateCommandList"); check(commands->Close(),"Close initial list");
        check(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"CreateFence");
        fenceEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!fenceEvent) throw std::runtime_error("CreateEvent failed");
        createTargets(); createScene();
        if(options.hair) initializeHair();
        selectScene(options.hair);
        if(options.view!="initial") { camera.yaw=options.view=="front"?0:options.view=="side"?-DirectX::XM_PIDIV2:DirectX::XM_PI; camera.pitch=0; }
    }
    void createScene() {
        D3D12_ROOT_PARAMETER param{}; param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; param.Constants.Num32BitValues=16; param.ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
        D3D12_ROOT_SIGNATURE_DESC rs{}; rs.NumParameters=1; rs.pParameters=&param; rs.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ComPtr<ID3DBlob> blob,errors; check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"Serialize root signature"); check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"CreateRootSignature");
        wchar_t exe[32768]; DWORD length=GetModuleFileNameW(nullptr,exe,32768); if(!length || length==32768) throw std::runtime_error("Executable path unavailable");
        fs::path shader=fs::path(exe).parent_path()/L"shaders/DebugScene.hlsl";
        auto compile=[&](const char* entry,const char* profile) {
            ComPtr<ID3DBlob> code,error; UINT flags=D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
            flags|=D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
            HRESULT hr=D3DCompileFromFile(shader.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,profile,flags,0,&code,&error);
            if(error) write(std::string(static_cast<char*>(error->GetBufferPointer()),error->GetBufferSize())); check(hr,"D3DCompileFromFile"); return code;
        };
        auto vs=compile("VSMain","vs_5_0"),ps=compile("PSMain","ps_5_0");
        D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"COLOR",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature=root.Get(); p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()}; p.InputLayout={layout,2};
        p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode=D3D12_CULL_MODE_BACK; p.RasterizerState.DepthClipEnable=TRUE;
        auto& blend=p.BlendState.RenderTarget[0]; blend.SrcBlend=D3D12_BLEND_ONE; blend.DestBlend=D3D12_BLEND_ZERO; blend.BlendOp=D3D12_BLEND_OP_ADD; blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_ZERO; blend.BlendOpAlpha=D3D12_BLEND_OP_ADD; blend.LogicOp=D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        p.DepthStencilState.DepthEnable=TRUE; p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL; p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
        p.DepthStencilState.StencilReadMask=255; p.DepthStencilState.StencilWriteMask=255;
        p.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS}; p.DepthStencilState.BackFace=p.DepthStencilState.FrontFace;
        p.SampleMask=UINT_MAX; p.NumRenderTargets=1; p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM; p.DSVFormat=DXGI_FORMAT_D32_FLOAT; p.SampleDesc.Count=1; p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        check(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&lines)),"Create line PSO"); p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; check(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&triangles)),"Create triangle PSO");
        D3D12_HEAP_PROPERTIES hp{}; hp.Type=D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC buffer{}; buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width=scene.vertices.size()*sizeof(Vertex); buffer.Height=1; buffer.DepthOrArraySize=1; buffer.MipLevels=1; buffer.SampleDesc.Count=1; buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        check(device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&vertexBuffer)),"Create vertex buffer");
        void* mapped{}; D3D12_RANGE read{0,0}; check(vertexBuffer->Map(0,&read,&mapped),"Map vertex buffer"); std::memcpy(mapped,scene.vertices.data(),static_cast<size_t>(buffer.Width)); vertexBuffer->Unmap(0,nullptr);
        vertexView={vertexBuffer->GetGPUVirtualAddress(),static_cast<UINT>(buffer.Width),sizeof(Vertex)};
    }
    void initUi() {
        IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGui::GetIO().IniFilename=nullptr; ImGui::GetIO().ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard; ImGui::StyleColorsDark();
        win32Ready=ImGui_ImplWin32_Init(window); if(!win32Ready) throw std::runtime_error("ImGui Win32 init failed");
        ImGui_ImplDX12_InitInfo info{}; info.Device=device.Get(); info.CommandQueue=queue.Get(); info.NumFramesInFlight=2; info.RTVFormat=DXGI_FORMAT_R8G8B8A8_UNORM; info.DSVFormat=DXGI_FORMAT_D32_FLOAT; info.SrvDescriptorHeap=srvHeap.Get(); info.UserData=this;
        info.SrvDescriptorAllocFn=[](ImGui_ImplDX12_InitInfo* i,D3D12_CPU_DESCRIPTOR_HANDLE* cpu,D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
            auto& a=*static_cast<Application*>(i->UserData); if(a.freeSrv.empty()) throw std::runtime_error("ImGui SRV heap exhausted"); unsigned index=a.freeSrv.back(); a.freeSrv.pop_back(); *cpu=a.srvHeap->GetCPUDescriptorHandleForHeapStart(); *gpu=a.srvHeap->GetGPUDescriptorHandleForHeapStart(); cpu->ptr+=index*a.srvStride; gpu->ptr+=index*a.srvStride;
        };
        info.SrvDescriptorFreeFn=[](ImGui_ImplDX12_InitInfo* i,D3D12_CPU_DESCRIPTOR_HANDLE cpu,D3D12_GPU_DESCRIPTOR_HANDLE) { auto& a=*static_cast<Application*>(i->UserData); a.freeSrv.push_back(static_cast<unsigned>((cpu.ptr-a.srvHeap->GetCPUDescriptorHandleForHeapStart().ptr)/a.srvStride)); };
        dx12Ready=ImGui_ImplDX12_Init(&info); if(!dx12Ready) throw std::runtime_error("ImGui DX12 init failed");
        if(!ImGui_ImplDX12_CreateDeviceObjects()) throw std::runtime_error("ImGui device objects failed");
    }
    void render() {
        unsigned index=swap->GetCurrentBackBufferIndex(); wait(frameFence[index]);reference.observe(index);
        if(dynamics.initialized&&windowSession.mode==3){bool wasFailed=dynamics.solver.failed;dynamics.observe(index);if(!wasFailed&&dynamics.solver.failed)write("Dynamics paused: "+dynamics.solver.failure);}
        ImGui_ImplDX12_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame(); camera.aspect=static_cast<float>(width)/height;
        // A bounded capture uses a fixed camera/settings regardless of desktop input.
        if(hairMode&&windowSession.mode==4){reference.refresh(camera);if(reference.current&&reference.current->failed){if(!options.referenceCaptureDir.empty()){NativeReferenceDemo::save(options.referenceCaptureDir/"failure.json",*reference.current);throw std::runtime_error(reference.current->status);}}if(referenceReady()&&!referenceStarted&&options.referenceCaptureDir.empty()){if(options.referenceKick!=std::array<float,3>{0,0,0})reference.kick(worldVectorToHair({options.referenceKick[0],options.referenceKick[1],options.referenceKick[2]},rest.worldToHairRotation));if(options.referenceRun)reference.run(true);referenceStarted=true;}}
        if(options.capture.empty()&&options.referenceCaptureDir.empty()) camera.update();
        ImGui::SetNextWindowPos({15,15},ImGuiCond_FirstUseEver); ImGui::SetNextWindowSize({370,0},ImGuiCond_FirstUseEver);
        if(windowSession.mode==4)ImGui::SetNextWindowSize({430,float(height)-30},ImGuiCond_Always);
        ImGui::Begin("HairLabDX12"); ImGui::TextWrapped("Adapter: %s",adapterName.c_str()); ImGui::Text("%s",warp?"WARP SOFTWARE":"Hardware rendering"); ImGui::Text("Viewport: %u x %u",width,height);
        ImGui::BeginDisabled(!options.capture.empty()||!options.referenceCaptureDir.empty());
        if(options.capture.empty()) {
            if(ImGui::RadioButton("Hair scene",hairMode)) { if(!restLoaded) initializeHair(); selectScene(true); }
            ImGui::SameLine(); if(ImGui::RadioButton("Bootstrap",!hairMode)) selectScene(false);
        }
        if(hairMode) hairPanel();
        if(hairMode){int previousMode=windowSession.mode;windowSession.panel(device.Get(),[&](const auto& record){immediate(record);},orientation);if(previousMode!=windowSession.mode){dynamics.modeChanged(previousMode,windowSession.mode,device.Get(),[&](const auto& record){immediate(record);});reference.run(false);if(windowSession.mode==4)initializeReference();}if(windowSession.mode==4)reference.panel(camera);if(windowSession.mode==3)dynamics.panel(device.Get(),[&](const auto& record){immediate(record);});if(windowSession.mode==1)orientation.panel(device.Get(),[&](const auto& record){immediate(record);});}
        auto& io=ImGui::GetIO(); ImGui::Text("FPS: %.1f | CPU frame: %.3f ms",io.Framerate,1000.0f/io.Framerate);
        DirectX::XMFLOAT3 eye; DirectX::XMStoreFloat3(&eye,camera.eye()); ImGui::Text("Position: %.2f %.2f %.2f",eye.x,eye.y,eye.z); ImGui::Text("Target: %.2f %.2f %.2f",camera.target.x,camera.target.y,camera.target.z); ImGui::Text("Distance: %.2f m",camera.distance);
        ImGui::Checkbox("Show grid",&scene.grid); ImGui::Checkbox("Show axes",&scene.axes); ImGui::Checkbox("Show test mesh",&scene.mesh); if(ImGui::Button("Reset camera (R)")) camera.reset(); ImGui::SameLine(); if(ImGui::Button("Frame (F)")) camera.frame(); ImGui::Checkbox("ImGui demo",&demo); ImGui::TextUnformatted("RMB orbit | MMB pan | Wheel zoom"); ImGui::EndDisabled(); ImGui::End(); if(demo) ImGui::ShowDemoWindow(&demo); ImGui::Render();
        check(allocators[index]->Reset(),"Reset allocator"); check(commands->Reset(allocators[index].Get(),nullptr),"Reset list");
        orientationUploads[index].clear();
        if(hairMode&&windowSession.mode==4)reference.record(commands.Get(),index);
        if(hairMode&&orientation.ready){windowSession.record(device.Get(),commands.Get(),orientationUploads[index],orientation);if(windowSession.mode==3){dynamics.record(device.Get(),commands.Get(),orientationUploads[index],index);if(dynamicsLoggedGeneration!=dynamics.solver.resetGeneration){dynamicsLoggedGeneration=dynamics.solver.resetGeneration;auto g=dynamics.solver.gravity;auto gw=dynamics.worldGravity;write("Dynamics config generation="+std::to_string(dynamicsLoggedGeneration)+" gravity world=["+std::to_string(gw.x)+","+std::to_string(gw.y)+","+std::to_string(gw.z)+"] gravity solver=["+std::to_string(g.x)+","+std::to_string(g.y)+","+std::to_string(g.z)+"] m/s^2, h="+std::to_string(dynamics.solver.settings.h)+", sweeps/substep="+std::to_string(dynamics.solver.sweeps));}}if(windowSession.mode==1)orientation.record(device.Get(),commands.Get(),orientationUploads[index]);orientation.state.position.transition(commands.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}
        D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition.pResource=targets[index].Get(); barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT; barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET; commands->ResourceBarrier(1,&barrier);
        auto color=rtv(index),depthHandle=dsvHeap->GetCPUDescriptorHandleForHeapStart(); const float clear[]={.055f,.075f,.105f,1}; commands->ClearRenderTargetView(color,clear,0,nullptr); commands->ClearDepthStencilView(depthHandle,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr); commands->OMSetRenderTargets(1,&color,FALSE,&depthHandle);
        D3D12_VIEWPORT vp{0,0,static_cast<float>(width),static_cast<float>(height),0,1}; D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)}; commands->RSSetViewports(1,&vp); commands->RSSetScissorRects(1,&scissor);
        commands->SetGraphicsRootSignature(root.Get()); DirectX::XMFLOAT4X4 matrix; DirectX::XMStoreFloat4x4(&matrix,camera.matrix(static_cast<float>(width)/height)); commands->SetGraphicsRoot32BitConstants(0,16,&matrix,0); commands->IASetVertexBuffers(0,1,&vertexView);
        commands->SetPipelineState(lines.Get()); commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST); if(scene.grid) commands->DrawInstanced(scene.gridCount,1,0,0); if(scene.axes) commands->DrawInstanced(6,1,scene.axesStart,0);
        if(scene.mesh) { commands->SetPipelineState(triangles.Get()); commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); commands->DrawInstanced(scene.meshCount,1,scene.meshStart,0); }
        if(hairMode){bool guides=preview.guides,render=preview.renderHair;if(windowSession.mode==4){preview.guides=false;preview.renderHair=false;}preview.draw(commands.Get(),matrix);preview.guides=guides;preview.renderHair=render;if(windowSession.mode==4)reference.draw(device.Get(),commands.Get(),root.Get(),lines.Get(),matrix,index);}
        if(hairMode&&windowSession.mode!=4)orientation.drawFrames(commands.Get(),matrix);
        bool referenceCaptureWanted=windowSession.mode==4&&!options.referenceCaptureDir.empty()&&referenceReady()&&!reference.current->failed&&(referenceWaitingReset?reference.current->step==0:reference.current->step==referenceNextCapture);
        bool captureFrame=referenceCaptureWanted||(!options.capture.empty() && rendered+1==frameLimit);
        if(captureFrame) capture.record(device.Get(),commands.Get(),targets[index].Get());
        ID3D12DescriptorHeap* heaps[]={srvHeap.Get()}; commands->SetDescriptorHeaps(1,heaps); ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(),commands.Get());
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); commands->ResourceBarrier(1,&barrier); check(commands->Close(),"Close list"); ID3D12CommandList* lists[]={commands.Get()}; queue->ExecuteCommandLists(1,lists);
        check(queue->Signal(fence.Get(),++serial),"Signal frame"); frameFence[index]=serial;
        check(swap->Present(1,0),"Present"); ++rendered;if(referenceCaptureWanted)finishReferenceCapture(index);
    }
    void run() {
        ImGui_ImplWin32_EnableDpiAwareness();
        WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=proc; wc.hInstance=instance; wc.lpszClassName=L"HairLabDX12"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        if(!RegisterClassExW(&wc)) throw std::runtime_error("RegisterClass failed"); registered=true;
        RECT rect{0,0,static_cast<LONG>(width),static_cast<LONG>(height)}; AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
        window=CreateWindowExW(0,wc.lpszClassName,L"HairLabDX12",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,this); if(!window) throw std::runtime_error("CreateWindow failed");
        initGraphics(); initUi(); write("Initialization OK: DX12 + depth-tested debug scene + ImGui"); ShowWindow(window,SW_SHOW); UpdateWindow(window);
        bool done=false; while(!done && !referenceSequenceDone && (!frameLimit || rendered<frameLimit)) {
            MSG msg{}; while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); if(msg.message==WM_QUIT) done=true; }
            if(done) break;
            if(minimized) { reference.run(false);if(dynamics.initialized)dynamics.suspend(); if(frameLimit||!options.referenceCaptureDir.empty()) throw std::runtime_error("Smoke test interrupted by minimization"); WaitMessage(); continue; }
            if(pendingWidth && pendingHeight) {
                if(width!=pendingWidth || height!=pendingHeight) { idle(); for(auto& t:targets) t.Reset(); depth.Reset(); width=pendingWidth; height=pendingHeight; check(swap->ResizeBuffers(2,width,height,DXGI_FORMAT_R8G8B8A8_UNORM,0),"ResizeBuffers"); createTargets(); write("Resized: "+std::to_string(width)+"x"+std::to_string(height)); }
                pendingWidth=pendingHeight=0;
            }
            render();
        }
        idle();
        if(windowSession.mode==4){reference.run(false);if(!options.referenceCaptureDir.empty()){validateRest();Submit submit=[&](const auto& record){immediate(record);};orientation.state.verifyImmutable(device.Get(),submit,true);write("PASS reference render bridge: original guide GPU positions/rest/velocity remain unchanged");}if(reference.current&&reference.current->failed)throw std::runtime_error(reference.current->status);if(options.referenceCaptureDir.empty()&&!options.report.empty()&&reference.current)NativeReferenceDemo::save(options.report,*reference.current);}
        if(!options.test.empty()) {
            OrientationTestContext context{device.Get(),[&](const auto& record){immediate(record);},executableDirectory()/"shaders/orientation",options.report,warp};
            orientation.state.verifyImmutable(device.Get(),context.submit,true);
            if(options.test=="guide-state")testGuideState(orientation.state,orientation.solver,context);
            validateRest();write("PASS orientation test + 120+ idle frames + unchanged guide/render position arrays: "+options.test);
        }
        if(options.validate) {
            validateRest(); finalValidated=true;
            if(orientation.ready){Submit submit=[&](const auto& record){immediate(record);};if(windowSession.mode<2){orientation.state.verifyImmutable(device.Get(),submit,windowSession.mode==0||options.resetAfter);write("Explicit orientation diagnostics: "+measureOrientation(orientation.cpu,orientation.solver,readOrientation(orientation.state,orientation.solver,device.Get(),submit)).json());}else orientation.state.verifyImmutable(device.Get(),submit,windowSession.mode==3?options.simReset:options.windowReset,true,windowSession.mode==3);}
        }
        if(frameLimit && rendered!=frameLimit) throw std::runtime_error("Smoke test closed before requested frame count");
        if(!options.capture.empty()) { capture.save(options.capture); write("PNG saved without ImGui: "+options.capture.string()); }
        if(windowSession.mode==3&&options.test.empty()&&(!options.report.empty()||options.validate||options.simSpecified)){
            Submit submit=[&](const auto& record){immediate(record);};auto result=dynamics.solver.diagnostics(device.Get(),submit,dynamics.solver.read(device.Get(),submit));result.pop_back();result+=",\"gravityWorld\":["+std::to_string(dynamics.worldGravity.x)+","+std::to_string(dynamics.worldGravity.y)+","+std::to_string(dynamics.worldGravity.z)+"],\"gravityFrame\":\"fixed frame0 neck world-to-local rotation\",\"renderedFrames\":"+std::to_string(rendered)+",\"discardedWallTimeSeconds\":"+std::to_string(dynamics.clock.discarded)+",\"deterministic\":"+(dynamics.deterministic?std::string("true"):std::string("false"))+"}";
            auto path=options.report.empty()?fs::path("artifacts/dynamics/report.json"):options.report;if(path.has_parent_path())fs::create_directories(path.parent_path());std::ofstream report(path);report<<result;report.flush();if(!report)throw std::runtime_error("Dynamics report write failed");write("Dynamics diagnostics: "+result);
        }else if(windowSession.mode==2&&options.test.empty()&&(!options.report.empty()||options.validate)){
            Submit submit=[&](const auto& record){immediate(record);};std::string result;
            if(windowSession.solver.ready())result=windowSession.solver.diagnostics(device.Get(),submit,windowSession.solver.read(device.Get(),submit));
            else{orientation.state.verifyImmutable(device.Get(),submit,true);result="{\"resetExact\":true,\"prepared\":false}";}
            auto path=options.report.empty()?fs::path("artifacts/window-report.json"):options.report;if(path.has_parent_path())fs::create_directories(path.parent_path());std::ofstream report(path);report<<result;report.flush();if(!report)throw std::runtime_error("Window report write failed");write("Window diagnostics: "+result);
        }else if(options.validate && options.test.empty()&&windowSession.mode!=4) { rest.report(options.report,rendered,initialValidated,finalValidated); write("Rest validation report: "+options.report.string()); }
        auditDebugMessages(); // Include GPU work submitted by the final diagnostic readbacks.
        write("Clean exit: rendered "+std::to_string(rendered)+" frames");
    }
};
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    Application app;
    try {
        app.options=AppOptions::parse(); app.warp=app.options.warp; app.frameLimit=app.options.frames; app.width=app.options.width; app.height=app.options.height;
        fs::path logPath=app.options.log;
        if(logPath.has_parent_path()) fs::create_directories(logPath.parent_path()); app.log.open(logPath); if(!app.log) throw std::runtime_error("Cannot open log file");
        app.run(); return 0;
    } catch(const std::exception& e) { app.write(std::string("FAILURE: ")+e.what()); return 1; }
}
