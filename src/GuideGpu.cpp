#include "GuideGpu.h"
#include <stdexcept>
namespace guidegpu {
void check(HRESULT h){if(FAILED(h))throw std::runtime_error("Guide DX12 failure: "+std::to_string(unsigned(h)));}
namespace {
Microsoft::WRL::ComPtr<ID3D12Resource> allocate(ID3D12Device* device,size_t bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state,bool uav=false) {
    D3D12_HEAP_PROPERTIES heap{};heap.Type=type;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max(size_t(16),bytes);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=uav?D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS:D3D12_RESOURCE_FLAG_NONE;
    Microsoft::WRL::ComPtr<ID3D12Resource> r;check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,state,nullptr,IID_PPV_ARGS(&r)));return r;
}
}
void Buffer::create(ID3D12Device* device,size_t size,bool uav,const wchar_t* name){bytes=std::max(size_t(16),size);resource=allocate(device,bytes,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_STATE_COMMON,uav);resource->SetName(name);state=D3D12_RESOURCE_STATE_COMMON;}
void Buffer::transition(ID3D12GraphicsCommandList* list,D3D12_RESOURCE_STATES next){if(state==next)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,state,next};list->ResourceBarrier(1,&b);state=next;}
void Buffer::upload(ID3D12Device* device,ID3D12GraphicsCommandList* list,const void* source,size_t size,UploadList& keep,D3D12_RESOURCE_STATES next){
    if(size>bytes)throw std::runtime_error("Guide upload overflow");auto staging=allocate(device,bytes,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);void* ptr;D3D12_RANGE noRead{0,0};check(staging->Map(0,&noRead,&ptr));std::memset(ptr,0,bytes);if(size)std::memcpy(ptr,source,size);staging->Unmap(0,nullptr);transition(list,D3D12_RESOURCE_STATE_COPY_DEST);list->CopyBufferRegion(resource.Get(),0,staging.Get(),0,bytes);transition(list,next);keep.push_back(staging);
}
Microsoft::WRL::ComPtr<ID3D12Resource> Buffer::readback(ID3D12Device* device,ID3D12GraphicsCommandList* list){auto staging=allocate(device,bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);auto previous=state;transition(list,D3D12_RESOURCE_STATE_COPY_SOURCE);list->CopyBufferRegion(staging.Get(),0,resource.Get(),0,bytes);transition(list,previous);return staging;}
void uavBarrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;b.UAV.pResource=resource;list->ResourceBarrier(1,&b);}
std::vector<char> mappedBytes(ID3D12Resource* staging,size_t bytes){void* ptr;D3D12_RANGE range{0,bytes};check(staging->Map(0,&range,&ptr));std::vector<char> raw(bytes);if(bytes)std::memcpy(raw.data(),ptr,bytes);D3D12_RANGE noWrite{0,0};staging->Unmap(0,&noWrite);return raw;}
void State::initialize(ID3D12Device* device,ID3D12GraphicsCommandList* list,const GuideCpuState& source,ID3D12Resource* current,ID3D12Resource* rest,UploadList& keep){
    cpu=&source;auto make=[&](Buffer& b,const auto& v,bool uav,const wchar_t* name){b.create(device,v.size()*sizeof(v[0]),uav,name);b.upload(device,list,v.data(),v.size()*sizeof(v[0]),keep,uav?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);};
    auto pos=[&](Buffer& b,ID3D12Resource* borrowed,const wchar_t* name){if(borrowed){b.resource=borrowed;b.bytes=source.source->positions.size()*12;b.state=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;}else{make(b,source.source->positions,&b==&position,name);b.transition(list,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);}};
    pos(position,current,L"GuideCurrentPosition");pos(restPosition,rest,L"GuideRestPosition");
    make(ranges,source.ranges,false,L"GuideStrandRanges");make(restOrientation,source.orientations,false,L"GuideRestOrientation");make(restLength,source.lengths,false,L"GuideRestLength");make(restBend,source.bends,false,L"GuideRestBendTwist");make(mass,source.masses,false,L"GuideMass");make(radius,source.radii,false,L"GuideRadius");make(orientation,source.orientations,true,L"GuideCurrentOrientation");make(velocity,source.velocities,true,L"GuideVelocity");
}
void State::reset(ID3D12Device* device,ID3D12GraphicsCommandList* list,const std::vector<hair::F>& q,UploadList& keep){if(q.size()!=cpu->orientations.size())throw std::runtime_error("Orientation reset count mismatch");orientation.upload(device,list,q.data(),q.size()*16,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);}
void State::verifyImmutable(ID3D12Device* device,const Submit& submit,bool requireRest,bool allowPositionChange,bool allowVelocityChange){
    struct Pending {Buffer* b;const void* cpu;size_t size;Microsoft::WRL::ComPtr<ID3D12Resource> staging;};std::vector<Pending> pending;
    auto add=[&](Buffer& b,const auto& values){pending.push_back({&b,values.data(),values.size()*sizeof(values[0]),{}});};
    if(!allowPositionChange)add(position,cpu->source->positions);add(restPosition,cpu->source->positions);add(ranges,cpu->ranges);add(restOrientation,cpu->orientations);add(restLength,cpu->lengths);add(restBend,cpu->bends);add(mass,cpu->masses);add(radius,cpu->radii);if(!allowVelocityChange)add(velocity,cpu->velocities);if(requireRest)add(orientation,cpu->orientations);
    submit([&](auto* list){for(auto& p:pending)p.staging=p.b->readback(device,list);});
    for(auto& p:pending){auto raw=mappedBytes(p.staging.Get(),p.size);if(p.size&&std::memcmp(raw.data(),p.cpu,p.size))throw std::runtime_error("Immutable guide / fixed position / zero velocity invariant failed");}
}
}
