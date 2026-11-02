#include "OrientationSolver.h"
#include <d3d12sdklayers.h>
#include <fstream>
using Microsoft::WRL::ComPtr;
unsigned OrientationSolver::participating() const{return settings.participating?std::min(settings.participating,unsigned(state->cpu->ranges.size())):unsigned(state->cpu->ranges.size());}
void OrientationSolver::initialize(ID3D12Device* device,ID3D12GraphicsCommandList* list,guidegpu::State& state_,const std::filesystem::path& shaders,UploadList& keep){
    state=&state_;D3D12_FEATURE_DATA_SHADER_MODEL feature{D3D_SHADER_MODEL_6_0};if(FAILED(device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&feature,sizeof(feature)))||feature.HighestShaderModel<D3D_SHADER_MODEL_6_0)throw std::runtime_error("Orientation compute requires supported Shader Model 6.0 hardware/runtime; no CPU fallback");
    std::ifstream stream(shaders/"GuideSolve.dxil",std::ios::binary|std::ios::ate);if(!stream)throw std::runtime_error("Missing compiled GuideSolve.dxil");auto size=stream.tellg();std::vector<char> code(size_t(size),0);stream.seekg(0);if(!stream.read(code.data(),size))throw std::runtime_error("DXIL load failed");
    D3D12_ROOT_PARAMETER params[9]{};params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[0].Constants.Num32BitValues=12;
    for(unsigned i=1;i<9;++i){params[i].ParameterType=i<=5?D3D12_ROOT_PARAMETER_TYPE_SRV:D3D12_ROOT_PARAMETER_TYPE_UAV;params[i].Descriptor.ShaderRegister=i<=5?i-1:i-6;}
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=9;desc.pParameters=params;ComPtr<ID3DBlob> blob,error;guidegpu::check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error));guidegpu::check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.CS={code.data(),code.size()};guidegpu::check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipeline)));
    errors.create(device,state->cpu->ranges.size()*4,true,L"OrientationStickyErrors");stats.create(device,state->cpu->ranges.size()*sizeof(SolveStats),true,L"OrientationRunStats");resetStatus(device,list,keep);
}
void OrientationSolver::resetStatus(ID3D12Device* device,ID3D12GraphicsCommandList* list,UploadList& keep){errors.upload(device,list,nullptr,0,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);stats.upload(device,list,nullptr,0,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);hasRun=false;}
void OrientationSolver::run(ID3D12GraphicsCommandList* list,unsigned sweeps){
    if(!sweeps||sweeps>4096||!(settings.baseSS>0)||!(settings.baseBT>0)||!std::isfinite(settings.baseSS)||!std::isfinite(settings.baseBT))throw std::runtime_error("Invalid orientation sweep count or stiffness");
    auto& cpu=*state->cpu;state->orientation.transition(list,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootSignature(root.Get());list->SetPipelineState(pipeline.Get());
    guidegpu::Buffer* inputs[]={&state->ranges,&state->position,&state->restLength,&state->restBend,&state->restOrientation};
    for(unsigned i=0;i<5;++i)list->SetComputeRootShaderResourceView(i+1,inputs[i]->address());
    list->SetComputeRootUnorderedAccessView(6,state->orientation.address());list->SetComputeRootUnorderedAccessView(7,errors.address());list->SetComputeRootUnorderedAccessView(8,stats.address());
    for(unsigned done=0;done<sweeps;done+=64){OrientationParameters p{unsigned(cpu.ranges.size()),unsigned(cpu.source->positions.size()),unsigned(cpu.orientations.size()),unsigned(cpu.bends.size()),participating(),std::min(64u,sweeps-done),settings.pinRoot?1u:0u,done==0?1u:0u,settings.baseSS,settings.baseBT,{}};
        list->SetComputeRoot32BitConstants(0,12,&p,0);list->Dispatch((p.participating+63)/64,1,1);
        guidegpu::uavBarrier(list,state->orientation.resource.Get());guidegpu::uavBarrier(list,errors.resource.Get());guidegpu::uavBarrier(list,stats.resource.Get());
    }hasRun=true;
}
