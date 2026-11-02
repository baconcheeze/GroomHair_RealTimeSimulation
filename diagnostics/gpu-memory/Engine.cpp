#include "Interleave.h"
#include "Layout.h"
namespace gpuls {
ComPtr<ID3D12Resource> heapBuffer(Context &c, size_t bytes,
                                  D3D12_HEAP_TYPE type,
                                  D3D12_RESOURCE_STATES state) {
  D3D12_HEAP_PROPERTIES h{};
  h.Type = type;
  D3D12_RESOURCE_DESC d{};
  d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  d.Width = std::max(size_t(16), bytes);
  d.Height = 1;
  d.DepthOrArraySize = 1;
  d.MipLevels = 1;
  d.SampleDesc.Count = 1;
  d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  ComPtr<ID3D12Resource> r;
  check(c.device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, state,
                                          nullptr, IID_PPV_ARGS(&r)));
  return r;
}
Engine::Engine(Context &context, size_t ic, size_t wc)
    : c(context), inputCapacity(ic), workCapacity(wc) {
  signature = root(c);
  auto dir = exeDir() / "shaders";
  const char *names[] = {"base", "work", "both"};
  for (unsigned i = 0; i < 3; i++) {
    std::string suffix = names[i];
    form[i] = pipeline(c, signature.Get(), dir / ("Form_" + suffix + ".dxil"));
    factor[i] =
        pipeline(c, signature.Get(), dir / ("Factor_" + suffix + ".dxil"));
    initial[i] =
        pipeline(c, signature.Get(), dir / ("Initial_" + suffix + ".dxil"));
    correct[i] =
        pipeline(c, signature.Get(), dir / ("Correct_" + suffix + ".dxil"));
    factorCheck[i] = pipeline(c, signature.Get(),
                              dir / ("Factor_check_" + suffix + ".dxil"));
    initialCheck[i] = pipeline(c, signature.Get(),
                               dir / ("Initial_check_" + suffix + ".dxil"));
    correctCheck[i] = pipeline(c, signature.Get(),
                               dir / ("Correct_check_" + suffix + ".dxil"));
  }
  ticks = heapBuffer(c, 40, D3D12_HEAP_TYPE_READBACK,
                     D3D12_RESOURCE_STATE_COPY_DEST);
  D3D12_QUERY_HEAP_DESC q{};
  q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  q.Count = 5;
  check(c.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
}
void Engine::ensure(size_t inBytes, size_t workBytes) {
  auto start = std::chrono::steady_clock::now();
  require(c.fence->GetCompletedValue() >= c.serial,
          "Cannot reallocate in-flight resources");
  if (!input.resource || inBytes > inputCapacity) {
    inputCapacity = std::max(inputCapacity, inBytes);
    input.create(c.device.Get(), inputCapacity, false, L"LayoutInputs");
    upload = heapBuffer(c, inputCapacity, D3D12_HEAP_TYPE_UPLOAD,
                        D3D12_RESOURCE_STATE_GENERIC_READ);
  }
  if (!work.resource || workBytes > workCapacity) {
    workCapacity = std::max(workCapacity, workBytes);
    work.create(c.device.Get(), workCapacity, true, L"LayoutWork");
    readback = heapBuffer(c, workCapacity, D3D12_HEAP_TYPE_READBACK,
                          D3D12_RESOURCE_STATE_COPY_DEST);
    UploadList keep;
    c.submit([&](auto *l) {
      work.upload(c.device.Get(), l, nullptr, 0, keep,
                  D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    });
  }
  allocationWall = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - start)
                       .count();
}
void Engine::prepare(const Pack &p) {
  auto packStart = std::chrono::steady_clock::now();
  wire.reset();
  if (layout)
    wire = std::make_shared<Wire>(p, layout);
  packingWall = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - packStart)
                    .count();
  const auto &encoded = wire ? wire->input : p.bytes;
  systems = unsigned(p.systems.size());
  usedWork = wire ? wire->workBytes : p.scratchBytes;
  metadata = p.systems;
  canonicalSeed.clear();
  physicalSeed.clear();
  usedInput=encoded.size();ensure(encoded.size(), usedWork);
  auto begin = std::chrono::steady_clock::now();
  void *v;
  D3D12_RANGE noRead{0, 0};
  check(upload->Map(0, &noRead, &v));
  std::memcpy(v, encoded.data(), encoded.size());
  upload->Unmap(0, nullptr);
  c.submit([&](auto *l) {
    input.transition(l, D3D12_RESOURCE_STATE_COPY_DEST);
    l->CopyBufferRegion(input.resource.Get(), 0, upload.Get(), 0,
                        encoded.size());
    input.transition(l, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  });
  uploadWall = std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - begin)
                   .count();
}
ReadResult Engine::run(unsigned count, bool mixed, bool formation,
                       bool timestamps) {
  require(count <= 5 && !mixed,
          "G64 only: mixed precision remains the historical negative control");
  require(launch.group == 32 && launch.tile == 2839 && !launch.legacy,
          "Launch fixed G32/tile2839");
  require(layout < 3, "Layout variant");
  unsigned variant = layout;
  unsigned end =
      rangeCount ? std::min(systems, rangeBase + rangeCount) : systems;
  ReadResult r;
  auto begin = std::chrono::steady_clock::now();
  c.submit([&](auto *l) {
    l->SetComputeRootSignature(signature.Get());
    l->SetComputeRootShaderResourceView(1, input.address());
    l->SetComputeRootUnorderedAccessView(2, work.address());
    auto stamp = [&](unsigned i) {
      if (timestamps)
        l->EndQuery(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, i);
    };
    auto dispatch = [&](ID3D12PipelineState *p) {
      l->SetPipelineState(p);
      for (unsigned base = rangeBase; base < end; base += launch.tile) {
        unsigned tileCount = std::min(launch.tile, end - base);
        unsigned cst[] = {systems, base, unsigned(formation), tileCount};
        l->SetComputeRoot32BitConstants(0, 4, cst, 0);
        l->Dispatch((tileCount + launch.group - 1) / launch.group, 1, 1);
      }
      guidegpu::uavBarrier(l, work.resource.Get());
    };
    stamp(0);
    if (formation)
      dispatch(form[variant].Get());
    stamp(1);
    dispatch(ownershipCheck ? factorCheck[variant].Get()
                            : factor[variant].Get());
    stamp(2);
    dispatch(ownershipCheck ? initialCheck[variant].Get()
                            : initial[variant].Get());
    stamp(3);
    for (unsigned k = 0; k < count; k++)
      dispatch(ownershipCheck ? correctCheck[variant].Get()
                              : correct[variant].Get());
    stamp(4);
    work.transition(l, D3D12_RESOURCE_STATE_COPY_SOURCE);
    l->CopyBufferRegion(readback.Get(), 0, work.resource.Get(), 0, usedWork);
    work.transition(l, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (timestamps)
      l->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 5,
                          ticks.Get(), 0);
  });
  auto rawWork = guidegpu::mappedBytes(readback.Get(), usedWork);
  auto unpackStart = std::chrono::steady_clock::now();
  if (wire) {
    if (!physicalSeed.empty())
      wire->guards(rawWork, physicalSeed);
    r.bytes = wire->decodeWork(rawWork, canonicalSeed);
  } else
    r.bytes = std::move(rawWork);
  r.unpacking = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - unpackStart)
                    .count();
  if (timestamps) {
    auto raw = guidegpu::mappedBytes(ticks.Get(), 40);
    uint64_t t[5];
    std::memcpy(t, raw.data(), 40);
    double scale = 1000. / c.frequency;
    r.formation = (t[1] - t[0]) * scale;
    r.factor = (t[2] - t[1]) * scale;
    r.initial = (t[3] - t[2]) * scale;
    r.corrections = (t[4] - t[3]) * scale;
    r.total = (t[4] - t[0]) * scale;
  }
  r.wall = std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now() - begin)
               .count();
  return r;
}
void Engine::seed(const std::vector<char> &canonicalBytes) {
  canonicalSeed = canonicalBytes;
  auto physical = wire ? wire->encodeWork(canonicalBytes) : canonicalBytes;
  if (wire)
    physicalSeed = physical;
  const auto &bytes = physical;
  require(bytes.size() == usedWork, "Seed length");
  auto staging = heapBuffer(c, usedWork, D3D12_HEAP_TYPE_UPLOAD,
                            D3D12_RESOURCE_STATE_GENERIC_READ);
  void *p;
  D3D12_RANGE noRead{0, 0};
  check(staging->Map(0, &noRead, &p));
  std::memcpy(p, bytes.data(), bytes.size());
  staging->Unmap(0, nullptr);
  c.submit([&](auto *l) {
    work.transition(l, D3D12_RESOURCE_STATE_COPY_DEST);
    l->CopyBufferRegion(work.resource.Get(), 0, staging.Get(), 0, bytes.size());
    work.transition(l, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  });
}
std::string Engine::memoryJSON() const {
  auto committed = [&](ID3D12Resource *r) {
    auto d = r->GetDesc();
    return c.device->GetResourceAllocationInfo(0, 1, &d).SizeInBytes;
  };
  std::ostringstream o;
  o << "{\"inputUsed\":" << usedInput
    << ",\"scratchUsed\":" << usedWork << ",\"inputCapacity\":" << input.bytes
    << ",\"scratchCapacity\":" << work.bytes
    << ",\"inputCommitted\":" << committed(input.resource.Get())
    << ",\"scratchCommitted\":" << committed(work.resource.Get())
    << ",\"uploadCommitted\":" << committed(upload.Get())
    << ",\"readbackCommitted\":" << committed(readback.Get())
    << ",\"timestampCommitted\":" << committed(ticks.Get());
  if (wire)
    o << ",\"canonicalScratch\":" << wire->canonicalBytes
      << ",\"numericWorkLogical\":" << wire->numericWork
      << ",\"numericInputLogical\":" << wire->numericInput
      << ",\"packingTiles\":" << wire->tiles.size()
      << ",\"descriptorBytes\":" << wire->tiles.size() * 32;
  if(wire){uint64_t maps=wire->maps.capacity()*sizeof(SystemMap);for(auto &m:wire->maps)maps+=m.input.capacity()*sizeof(Span);o<<",\"cpuEncodedInputCapacity\":"<<wire->input.capacity()<<",\"cpuMappingCapacity\":"<<maps+wire->tiles.capacity()*sizeof(Tile)+wire->canonical.capacity()*sizeof(Meta);}
  o << '}';
  return o.str();
}
} // namespace gpuls
