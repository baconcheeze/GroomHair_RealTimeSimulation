#include "GraphicsTimings.h"
#include <stdexcept>
#include <iomanip>

namespace { void Verify(HRESULT h){if(FAILED(h))throw std::runtime_error("Graphics timestamp operation failed");} }
void GraphicsTimings::Init(ID3D12Device *device,ID3D12CommandQueue *queue,const std::filesystem::path &path){
  Verify(queue->GetTimestampFrequency(&frequency));
  if(!frequency)throw std::runtime_error("Graphics timestamp frequency is zero");
  D3D12_QUERY_HEAP_DESC query{};query.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;query.Count=2*Queries;
  Verify(device->CreateQueryHeap(&query,IID_PPV_ARGS(&heap)));
  D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width=2*Queries*sizeof(UINT64);desc.Height=1;desc.DepthOrArraySize=1;
  desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  Verify(device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
  if(path.has_parent_path())std::filesystem::create_directories(path.parent_path());
  csv.open(path);if(!csv)throw std::runtime_error("Cannot create graphics timing CSV");
  csv<<"frame,step,stage,gpu_ms,queue_frequency_hz\n"<<std::setprecision(10);
}
void GraphicsTimings::Collect(UINT64 completedFence){
  if(!heap)return;
  static const char *names[]={"GraphicsTotal","Ground","HeadBody","Jacket","RenderHairDraw","GuideDraw","CollisionDebugDraw","BoneDebugDraw","MSAAResolve","ImGui"};
  for(unsigned index=0;index<slots.size();++index){auto &slot=slots[index];
    if(!slot.fence||slot.fence>completedFence)continue;
    D3D12_RANGE range{index*Queries*sizeof(UINT64),(index+1)*Queries*sizeof(UINT64)};
    void *mapped=nullptr;Verify(readback->Map(0,&range,&mapped));const auto *values=static_cast<const UINT64*>(mapped);
    for(unsigned s=0;s<StageCount;++s){last[s]=0;if(!(slot.mask&(1u<<s)))continue;
      UINT64 start=values[index*Queries+2*s],end=values[index*Queries+2*s+1];
      if(end<start){readback->Unmap(0,nullptr);throw std::runtime_error("Reversed graphics timestamps");}
      last[s]=double(end-start)*1000.0/double(frequency);
      csv<<slot.frame<<','<<slot.step<<','<<names[s]<<','<<last[s]<<','<<frequency<<'\n';
    }
    D3D12_RANGE written{0,0};readback->Unmap(0,&written);slot.fence=0;
  }
}
void GraphicsTimings::BeginFrame(ID3D12GraphicsCommandList *cmd,unsigned index,unsigned frame,unsigned step){
  if(!heap)return;if(index>=slots.size()||slots[index].fence)throw std::runtime_error("Graphics timing slot still in flight");
  currentSlot=index;slots[index]={0,0,frame,step};Begin(cmd,Total);
}
void GraphicsTimings::Begin(ID3D12GraphicsCommandList *cmd,Stage stage){
  if(!heap)return;slots[currentSlot].mask|=1u<<stage;
  cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,currentSlot*Queries+2*stage);
}
void GraphicsTimings::End(ID3D12GraphicsCommandList *cmd,Stage stage){
  if(heap)cmd->EndQuery(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,currentSlot*Queries+2*stage+1);
}
void GraphicsTimings::EndFrame(ID3D12GraphicsCommandList *cmd,UINT64 completionFence){
  if(!heap)return;End(cmd,Total);auto &slot=slots[currentSlot];
  // Resolve only initialized query pairs; disabled draw stages have no queries.
  for(unsigned stage=0;stage<StageCount;++stage)if(slot.mask&(1u<<stage))
    cmd->ResolveQueryData(heap.Get(),D3D12_QUERY_TYPE_TIMESTAMP,currentSlot*Queries+2*stage,2,readback.Get(),(currentSlot*Queries+2*stage)*sizeof(UINT64));
  slot.fence=completionFence;
}
