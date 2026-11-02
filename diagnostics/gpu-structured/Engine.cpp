#include "Layout.h"
namespace gpuls
{
ComPtr<ID3D12Resource> heapBuffer(Context &c, size_t bytes, D3D12_HEAP_TYPE type, D3D12_RESOURCE_STATES state)
{
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
    check(c.device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r)));
    return r;
}
Engine::Engine(Context &context, size_t ic, size_t wc) : c(context), inputCapacity(ic), workCapacity(wc)
{
    signature = root(c);
    auto dir = exeDir() / "shaders";
    form = pipeline(c, signature.Get(), dir / "Form.dxil");
    factor[0] = pipeline(c, signature.Get(), dir / "Factor.dxil");
    initial[0] = pipeline(c, signature.Get(), dir / "Initial.dxil");
    correct[0] = pipeline(c, signature.Get(), dir / "Correct.dxil");
    factor[1] = pipeline(c, signature.Get(), dir / "Factor32.dxil");
    initial[1] = pipeline(c, signature.Get(), dir / "Initial32.dxil");
    correct[1] = pipeline(c, signature.Get(), dir / "Correct32.dxil");
    input.create(c.device.Get(), ic, false, L"PilotInputs");
    work.create(c.device.Get(), wc, true, L"PilotWorkspace");
    upload = heapBuffer(c, ic, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    readback = heapBuffer(c, wc, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    ticks = heapBuffer(c, 40, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_QUERY_HEAP_DESC q{};
    q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    q.Count = 5;
    check(c.device->CreateQueryHeap(&q, IID_PPV_ARGS(&queries)));
    UploadList keep;
    c.submit([&](auto *l) { work.upload(c.device.Get(), l, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); });
}
void Engine::prepare(const Pack &p)
{
    require(p.bytes.size() <= inputCapacity && p.scratchBytes <= workCapacity, "Pilot preallocated capacity exceeded");
    systems = unsigned(p.systems.size());
    usedWork = p.scratchBytes;
    metadata = p.systems;
    auto begin = std::chrono::steady_clock::now();
    void *v;
    D3D12_RANGE noRead{0, 0};
    check(upload->Map(0, &noRead, &v));
    std::memcpy(v, p.bytes.data(), p.bytes.size());
    upload->Unmap(0, nullptr);
    c.submit([&](auto *l) {
        input.transition(l, D3D12_RESOURCE_STATE_COPY_DEST);
        l->CopyBufferRegion(input.resource.Get(), 0, upload.Get(), 0, p.bytes.size());
        input.transition(l, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    });
    uploadWall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
}
ReadResult Engine::run(unsigned count, bool mixed, bool formation, bool timestamps)
{
    require(count <= 5, "Correction cap");
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
            for (unsigned base = 0; base < systems; base += 100)
            {
                unsigned cst[] = {systems, base, unsigned(formation), 0};
                l->SetComputeRoot32BitConstants(0, 4, cst, 0);
                l->Dispatch(std::min(100u, systems - base), 1, 1);
            }
            guidegpu::uavBarrier(l, work.resource.Get());
        };
        stamp(0);
        if (formation)
            dispatch(form.Get());
        stamp(1);
        dispatch(factor[mixed].Get());
        stamp(2);
        dispatch(initial[mixed].Get());
        stamp(3);
        for (unsigned k = 0; k < count; k++)
            dispatch(correct[mixed].Get());
        stamp(4);
        work.transition(l, D3D12_RESOURCE_STATE_COPY_SOURCE);
        l->CopyBufferRegion(readback.Get(), 0, work.resource.Get(), 0, usedWork);
        work.transition(l, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        if (timestamps)
            l->ResolveQueryData(queries.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 5, ticks.Get(), 0);
    });
    r.bytes = guidegpu::mappedBytes(readback.Get(), usedWork);
    if (timestamps)
    {
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
    r.wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    return r;
}
} // namespace gpuls
