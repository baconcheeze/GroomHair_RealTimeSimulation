#include "OrientationSession.h"
#include "imgui.h"
#include <d3dcompiler.h>
using Microsoft::WRL::ComPtr;
void OrientationSession::initialize(ID3D12Device* device,const Submit& submit,const Strands& guide,ID3D12Resource* current,ID3D12Resource* rest,ID3D12Resource* connectivity,const std::filesystem::path& shaders){
    cpu.initialize(guide);segmentStarts=connectivity;UploadList keep;submit([&](auto* list){state.initialize(device,list,cpu,current,rest,keep);solver.initialize(device,list,state,shaders,keep);});createFrames(device,shaders);ready=true;
}
void OrientationSession::startup(ID3D12Device* device,const Submit& submit,const std::string& mode,unsigned sweeps,bool resetAfter){UploadList keep;submit([&](auto* list){if(!mode.empty()){auto q=cpu.perturb(mode,solver.participating(),solver.settings.pinRoot);state.reset(device,list,q,keep);solver.resetStatus(device,list,keep);}if(sweeps)solver.run(list,sweeps);if(resetAfter){state.reset(device,list,cpu.orientations,keep);solver.resetStatus(device,list,keep);}});}
void OrientationSession::panel(ID3D12Device* device,const Submit& submit){
    if(!ready)return;ImGui::SeparatorText("Orientation only (fixed positions)");
    ImGui::TextWrapped("Hair lines stay fixed. Bend/twist rotates ONE middle frame; inspect the RGB axes.");
    if(!actionNotice.empty())ImGui::TextWrapped("%s",actionNotice.c_str());
    const char* selections[]={"1","10","100","All"};int selected=solver.settings.participating==1?0:solver.settings.participating==10?1:solver.settings.participating==100?2:3;
    if(ImGui::Combo("Participating strands",&selected,selections,4)){solver.settings.participating=selected==0?1:selected==1?10:selected==2?100:0;pending="rest";pendingSweeps=0;}
    if(ImGui::Checkbox("Pin root orientation",&solver.settings.pinRoot)){pending="rest";pendingSweeps=0;}
    int sweeps=int(solver.settings.sweeps);if(ImGui::SliderInt("Sweeps",&sweeps,1,4096))solver.settings.sweeps=unsigned(sweeps);
    if(ImGui::Button("Reset Orientations")){pending="rest";pendingSweeps=0;}
    bool firstMode=true;
    for(const char* mode:{"bend","twist","smooth","random"}){
        if(!firstMode)ImGui::SameLine();firstMode=false;
        if(ImGui::Button(mode)){
            pending=mode;pendingSweeps=0;
            // Middle segment 15 is invisible at the default stride 4 in this groom.
            // This changes debug visibility only; source perturbation math is untouched.
            solver.settings.showFrames=true;solver.settings.frameStride=1;
            solver.settings.axisLength=std::max(solver.settings.axisLength,.008f);
            solver.settings.autoSolve=false;
        }
    }
    if(ImGui::Button("Run One Sweep"))pendingSweeps=1;ImGui::SameLine();if(ImGui::Button("Run N Sweeps"))pendingSweeps=solver.settings.sweeps;
    ImGui::Checkbox("Auto Solve",&solver.settings.autoSolve);ImGui::Checkbox("Material frames (overlay)",&solver.settings.showFrames);
    int limit=int(solver.settings.frameLimit),stride=int(solver.settings.frameStride);if(ImGui::SliderInt("Frame strands",&limit,1,10))solver.settings.frameLimit=unsigned(limit);if(ImGui::SliderInt("Frame stride",&stride,1,32))solver.settings.frameStride=unsigned(stride);
    ImGui::SliderFloat("Axis length (m)",&solver.settings.axisLength,.0005f,.02f,"%.4f");
    if(ImGui::Button("Read Diagnostics")){try{UploadList keep;if(!pending.empty()||pendingSweeps)submit([&](auto* list){record(device,list,keep,false);});state.verifyImmutable(device,submit,false);lastDiagnostics=measureOrientation(cpu,solver,readOrientation(state,solver,device,submit)).json();}catch(const std::exception& e){lastDiagnostics=e.what();}}
    if(!lastDiagnostics.empty())ImGui::TextWrapped("%s",lastDiagnostics.c_str());
}
void OrientationSession::record(ID3D12Device* device,ID3D12GraphicsCommandList* list,UploadList& keep,bool allowAuto){
    if(!pending.empty()){auto q=cpu.perturb(pending,solver.participating(),solver.settings.pinRoot);state.reset(device,list,q,keep);solver.resetStatus(device,list,keep);actionNotice=pending=="rest"?"Orientations reset to rest.":pending+" applied. RGB axes enabled, stride 1; Auto Solve paused.";pending.clear();lastDiagnostics.clear();}
    if(pendingSweeps){solver.run(list,pendingSweeps);pendingSweeps=0;}else if(allowAuto&&solver.settings.autoSolve)solver.run(list,solver.settings.sweeps);
}
void OrientationSession::createFrames(ID3D12Device* device,const std::filesystem::path& shaders){
    D3D12_ROOT_PARAMETER params[4]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=20;params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;
    for(unsigned i=1;i<4;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;params[i].Descriptor.ShaderRegister=i-1;params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX;}
    D3D12_ROOT_SIGNATURE_DESC rs{};rs.NumParameters=4;rs.pParameters=params;ComPtr<ID3DBlob> blob,error;guidegpu::check(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));guidegpu::check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&frameRoot)));
    auto compile=[&](const char* entry,const char* profile){ComPtr<ID3DBlob> code,errors;auto path=shaders/"MaterialFrames.hlsl";HRESULT hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);if(FAILED(hr))throw std::runtime_error(errors?static_cast<const char*>(errors->GetBufferPointer()):"Material frame shader compile failure");return code;};auto vs=compile("VSMain","vs_5_0"),ps=compile("PSMain","ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{};p.pRootSignature=frameRoot.Get();p.VS={vs->GetBufferPointer(),vs->GetBufferSize()};p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;p.SampleMask=UINT_MAX;p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.DSVFormat=DXGI_FORMAT_D32_FLOAT;p.SampleDesc.Count=1;p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;p.RasterizerState.DepthClipEnable=TRUE;
    // Match source debug-only axis overlay. Hair/head still use their normal depth PSOs.
    p.DepthStencilState.DepthEnable=TRUE;p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;p.DepthStencilState.StencilReadMask=p.DepthStencilState.StencilWriteMask=255;p.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};p.DepthStencilState.BackFace=p.DepthStencilState.FrontFace;
    auto& b=p.BlendState.RenderTarget[0];b.SrcBlend=D3D12_BLEND_ONE;b.DestBlend=D3D12_BLEND_ZERO;b.BlendOp=D3D12_BLEND_OP_ADD;b.SrcBlendAlpha=D3D12_BLEND_ONE;b.DestBlendAlpha=D3D12_BLEND_ZERO;b.BlendOpAlpha=D3D12_BLEND_OP_ADD;b.LogicOp=D3D12_LOGIC_OP_NOOP;b.RenderTargetWriteMask=15;guidegpu::check(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&framePso)));
}
void OrientationSession::drawFrames(ID3D12GraphicsCommandList* list,const DirectX::XMFLOAT4X4& matrix){
    if(!ready||!solver.settings.showFrames)return;auto& settings=solver.settings;unsigned strands=std::min(solver.participating(),settings.frameLimit);if(!strands)return;auto last=cpu.ranges[strands-1];unsigned segments=last.firstSegment+last.segmentCount;
    state.orientation.transition(list,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);list->SetGraphicsRootSignature(frameRoot.Get());list->SetPipelineState(framePso.Get());list->SetGraphicsRoot32BitConstants(0,16,&matrix,0);list->SetGraphicsRoot32BitConstants(0,1,&settings.frameStride,16);list->SetGraphicsRoot32BitConstants(0,1,&settings.axisLength,17);uint32_t pad[2]{};list->SetGraphicsRoot32BitConstants(0,2,pad,18);list->SetGraphicsRootShaderResourceView(1,state.position.address());list->SetGraphicsRootShaderResourceView(2,state.orientation.address());list->SetGraphicsRootShaderResourceView(3,segmentStarts->GetGPUVirtualAddress());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);list->DrawInstanced(((segments+settings.frameStride-1)/settings.frameStride)*6,1,0,0);
}
