#include "StaticPreview.h"
#include <d3dcompiler.h>
#include <cstring>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
namespace {
void ck(HRESULT hr) { if(FAILED(hr)) throw std::runtime_error("Static preview DX12 failure: "+std::to_string(static_cast<unsigned>(hr))); }
ComPtr<ID3D12Resource> buffer(ID3D12Device* d,size_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=type;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=bytes; desc.Height=1; desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    ComPtr<ID3D12Resource> r; ck(d->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r))); return r;
}
void transition(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES a,D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; barrier.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,a,b}; list->ResourceBarrier(1,&barrier);
}
ComPtr<ID3DBlob> compile(const std::filesystem::path& path,const char* entry,const char* target) {
    ComPtr<ID3DBlob> code,error; UINT flags=D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags|=D3DCOMPILE_DEBUG|D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    HRESULT hr=D3DCompileFromFile(path.c_str(),nullptr,D3D_COMPILE_STANDARD_FILE_INCLUDE,entry,target,flags,0,&code,&error);
    if(FAILED(hr)) throw std::runtime_error(error?static_cast<const char*>(error->GetBufferPointer()):"Preview shader compile failed"); return code;
}
}
void StaticPreview::initialize(ID3D12Device* device,ID3D12GraphicsCommandList* list,const RestData& data,const std::filesystem::path& shaders) {
    auto add=[&](const void* cpu,size_t bytes,const char* name,D3D12_RESOURCE_STATES state) {
        if(!bytes || bytes>UINT_MAX) throw std::runtime_error("Unsupported GPU buffer size");
        PreviewBuffer b; b.cpu=cpu; b.bytes=bytes; b.name=name; b.state=state;
        b.gpu=buffer(device,bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,std::strcmp(name,"GuideCurrentPosition")==0);
        std::wstring wide(b.name.begin(),b.name.end()); b.gpu->SetName(wide.c_str());
        auto upload=buffer(device,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ); void* p; D3D12_RANGE read{0,0}; ck(upload->Map(0,&read,&p)); std::memcpy(p,cpu,bytes); upload->Unmap(0,nullptr);
        transition(list,b.gpu.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(b.gpu.Get(),0,upload.Get(),0,bytes); transition(list,b.gpu.Get(),D3D12_RESOURCE_STATE_COPY_DEST,state); uploads.push_back(upload); buffers.push_back(std::move(b)); return buffers.size()-1;
    };
    for(auto pair:{std::pair{&data.guide,"Guide"},std::pair{&data.render,"Render"}}) {
        auto& s=*pair.first;
        std::string prefix=pair.second;
        add(s.positions.data(),s.positions.size()*12,(prefix+"RestPosition").c_str(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        add(s.positions.data(),s.positions.size()*12,(prefix+"CurrentPosition").c_str(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        add(s.segments.data(),s.segments.size()*4,(prefix+"SegmentStarts").c_str(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    auto mesh=[&](const MeshData& m,const char* v,const char* i,size_t& vb,size_t& ib) { vb=add(m.vertices.data(),m.vertices.size()*sizeof(HeadVertex),v,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER); ib=add(m.indices.data(),m.indices.size()*4,i,D3D12_RESOURCE_STATE_INDEX_BUFFER); };
    mesh(data.selectedHead,"SelectedHeadVertices","SelectedHeadIndices",selectedVB,selectedIB); mesh(data.originalHead,"OriginalHeadVertices","OriginalHeadIndices",originalVB,originalIB);
    if(!data.hullEdges.empty()) { hullVB=add(data.hullPositions.data(),data.hullPositions.size()*12,"HullVertices",D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER); hullIB=add(data.hullEdges.data(),data.hullEdges.size()*4,"HullEdgeIndices",D3D12_RESOURCE_STATE_INDEX_BUFFER); }
    selectedIndices=static_cast<UINT>(data.selectedHead.indices.size()); originalIndices=static_cast<UINT>(data.originalHead.indices.size()); hullIndices=static_cast<UINT>(data.hullEdges.size()); guideVertices=static_cast<UINT>(data.guide.segments.size()*2); renderVertices=static_cast<UINT>(data.render.segments.size()*2);
    D3D12_ROOT_PARAMETER params[3]{}; params[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[0].Constants.Num32BitValues=20; params[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    for(UINT i=1;i<3;++i) { params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV; params[i].Descriptor.ShaderRegister=i-1; params[i].ShaderVisibility=D3D12_SHADER_VISIBILITY_VERTEX; }
    D3D12_ROOT_SIGNATURE_DESC rs{}; rs.NumParameters=3; rs.pParameters=params; rs.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob,error; ck(D3D12SerializeRootSignature(&rs,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&error)); ck(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
    auto path=shaders/"StaticPreview.hlsl"; auto hv=compile(path,"HairVS","vs_5_0"),mv=compile(path,"HeadVS","vs_5_0"),wv=compile(path,"HullVS","vs_5_0"),flat=compile(path,"FlatPS","ps_5_0"),lit=compile(path,"HeadPS","ps_5_0");
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature=root.Get(); p.SampleMask=UINT_MAX; p.NumRenderTargets=1; p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM; p.DSVFormat=DXGI_FORMAT_D32_FLOAT; p.SampleDesc.Count=1;
    p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode=D3D12_CULL_MODE_BACK; p.RasterizerState.DepthClipEnable=TRUE;
    auto& b=p.BlendState.RenderTarget[0]; b.SrcBlend=D3D12_BLEND_ONE; b.DestBlend=D3D12_BLEND_ZERO; b.BlendOp=D3D12_BLEND_OP_ADD; b.SrcBlendAlpha=D3D12_BLEND_ONE; b.DestBlendAlpha=D3D12_BLEND_ZERO; b.BlendOpAlpha=D3D12_BLEND_OP_ADD; b.LogicOp=D3D12_LOGIC_OP_NOOP; b.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    p.DepthStencilState.DepthEnable=TRUE; p.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL; p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS_EQUAL;
    p.DepthStencilState.StencilReadMask=p.DepthStencilState.StencilWriteMask=255; p.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS}; p.DepthStencilState.BackFace=p.DepthStencilState.FrontFace;
    p.VS={hv->GetBufferPointer(),hv->GetBufferSize()}; p.PS={flat->GetBufferPointer(),flat->GetBufferSize()}; p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    ck(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&hairPso)));
    D3D12_INPUT_ELEMENT_DESC layout[]={{"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},{"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    p.InputLayout={layout,2}; p.VS={mv->GetBufferPointer(),mv->GetBufferSize()}; p.PS={lit->GetBufferPointer(),lit->GetBufferSize()}; p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; ck(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&headPso)));
    p.InputLayout={layout,1}; p.VS={wv->GetBufferPointer(),wv->GetBufferSize()}; p.PS={flat->GetBufferPointer(),flat->GetBufferSize()}; p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE; ck(device->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&hullPso)));
}
void StaticPreview::draw(ID3D12GraphicsCommandList* list,const DirectX::XMFLOAT4X4& matrix) {
    list->SetGraphicsRootSignature(root.Get()); list->SetGraphicsRoot32BitConstants(0,16,&matrix,0);
    auto color=[&](float r,float g,float b) { float c[]{r,g,b,1}; list->SetGraphicsRoot32BitConstants(0,4,c,16); };
    auto indexed=[&](size_t vb,size_t ib,UINT count,UINT stride) { D3D12_VERTEX_BUFFER_VIEW v{buffers[vb].gpu->GetGPUVirtualAddress(),static_cast<UINT>(buffers[vb].bytes),stride}; D3D12_INDEX_BUFFER_VIEW i{buffers[ib].gpu->GetGPUVirtualAddress(),static_cast<UINT>(buffers[ib].bytes),DXGI_FORMAT_R32_UINT}; list->IASetVertexBuffers(0,1,&v); list->IASetIndexBuffer(&i); list->DrawIndexedInstanced(count,1,0,0,0); };
    if(head) { color(.6f,.6f,.6f); list->SetPipelineState(headPso.Get()); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); indexed(original?originalVB:selectedVB,original?originalIB:selectedIB,original?originalIndices:selectedIndices,sizeof(HeadVertex)); }
    list->SetPipelineState(hairPso.Get()); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
    if(renderHair) { color(.48f,.27f,.12f); list->SetGraphicsRootShaderResourceView(1,buffers[4].gpu->GetGPUVirtualAddress()); list->SetGraphicsRootShaderResourceView(2,buffers[5].gpu->GetGPUVirtualAddress()); list->DrawInstanced(renderVertices,1,0,0); }
    if(guides) { color(.05f,1,1); list->SetGraphicsRootShaderResourceView(1,buffers[1].gpu->GetGPUVirtualAddress()); list->SetGraphicsRootShaderResourceView(2,buffers[2].gpu->GetGPUVirtualAddress()); list->DrawInstanced(guideVertices,1,0,0); }
    if(hulls && hullIndices) { color(1,.45f,.02f); list->SetPipelineState(hullPso.Get()); indexed(hullVB,hullIB,hullIndices,12); }
}
void StaticPreview::recordValidation(ID3D12Device* device,ID3D12GraphicsCommandList* list,bool movingGuide) {
    for(size_t i=0;i<6;++i) { auto& b=buffers[i]; if(i==1&&movingGuide){b.readback.Reset();continue;} b.readback=buffer(device,b.bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST); transition(list,b.gpu.Get(),b.state,D3D12_RESOURCE_STATE_COPY_SOURCE); list->CopyBufferRegion(b.readback.Get(),0,b.gpu.Get(),0,b.bytes); transition(list,b.gpu.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,b.state); }
}
void StaticPreview::finishValidation() {
    for(size_t i=0;i<6;++i) { auto& b=buffers[i]; if(!b.readback)continue; void* data; D3D12_RANGE range{0,b.bytes}; ck(b.readback->Map(0,&range,&data)); bool equal=std::memcmp(data,b.cpu,b.bytes)==0; D3D12_RANGE written{0,0}; b.readback->Unmap(0,&written); b.readback.Reset(); if(!equal) throw std::runtime_error("GPU readback mismatch: "+b.name); }
}
