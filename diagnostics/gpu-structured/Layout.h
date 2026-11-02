#pragma once
#include "../native-ls/NativeLS.h"
#include "Context.h"
#include <cstddef>
namespace gpuls
{
struct Meta
{
    uint32_t n, blocks, rows, rowOffset, blockOffset, diagOffset, lowerOffset, rhsOffset, scaleOffset, workOffset,
        statusOffset, outputOffset, frameBytes, mapOffset, pad0, pad1;
};
struct Block
{
    uint32_t size, start;
};
struct Row
{
    uint32_t count, pad;
    uint32_t col[6];
    double b, a[6];
};
struct Map
{
    uint32_t station, slot, kind, index;
};
struct Status
{
    uint32_t code, stage, index, corrections, lost, stagnation, done, pad;
};
static_assert(sizeof(double) == 8 && sizeof(nativels::Mat) == 288 && offsetof(Meta, workOffset) == 36 &&
              offsetof(Meta, mapOffset) == 52 && offsetof(Row, col) == 8);
static_assert(sizeof(Meta) == 64 && sizeof(Block) == 8 && sizeof(Row) == 88 && offsetof(Row, b) == 32 &&
              offsetof(Row, a) == 40 && sizeof(Status) == 32 && sizeof(Map) == 16);
struct Addresses
{
    uint32_t L, C, u, y, du, s, steps, status, diag, lower, rhs, end;
};
inline Addresses addresses(uint32_t base, unsigned n, unsigned blocks)
{
    Addresses w;
    w.L = base;
    w.C = w.L + blocks * 288;
    w.u = w.C + blocks * 288;
    w.y = w.u + n * 8;
    w.du = w.y + n * 8;
    w.s = w.du + n * 8;
    w.steps = w.s + n * 8;
    w.status = w.steps + 40;
    w.diag = w.status + 32;
    w.lower = w.diag + blocks * 288;
    w.rhs = w.lower + blocks * 288;
    w.end = w.rhs + n * 8;
    return w;
}
struct Pack
{
    std::vector<char> bytes;
    std::vector<Meta> systems;
    uint32_t scratchBytes = 0;
    explicit Pack(unsigned count) : bytes(count * sizeof(Meta)), systems(count)
    {
    }
    template <class T> uint32_t append(const T *p, size_t count)
    {
        while (bytes.size() % 8)
            bytes.push_back(0);
        uint32_t start = uint32_t(bytes.size());
        require(bytes.size() + count * sizeof(T) < UINT32_MAX, "Input capacity overflow");
        bytes.resize(bytes.size() + count * sizeof(T));
        if (count)
            std::memcpy(bytes.data() + start, p, count * sizeof(T));
        return start;
    }
    void system(unsigned i, const nativels::Workspace &w)
    {
        Meta m{};
        m.n = w.n;
        m.blocks = unsigned(w.size.size());
        m.rows = unsigned(w.scaled.size());
        std::vector<Block> blocks;
        for (unsigned j = 0; j < w.size.size(); j++)
            blocks.push_back({w.size[j], w.start[j]});
        std::vector<Row> rows;
        for (auto &r : w.scaled)
        {
            Row v{};
            v.count = r.j.count;
            v.b = r.r;
            for (unsigned k = 0; k < v.count; k++)
            {
                v.col[k] = r.j.data[k].first;
                v.a[k] = r.j.data[k].second;
            }
            rows.push_back(v);
        }
        std::vector<Map> maps;
        for (unsigned j = 0; j < w.n; j++)
        {
            Map v{w.station[j], w.slot[j], 0, 0};
            for (unsigned z = 0; z < w.layout.x.size(); z++)
                if (w.layout.x[z] >= 0 && j >= unsigned(w.layout.x[z]) && j < unsigned(w.layout.x[z]) + 3)
                    v.index = z * 3 + j - unsigned(w.layout.x[z]);
            for (unsigned z = 0; z < w.layout.q.size(); z++)
                if (w.layout.q[z] >= 0 && j >= unsigned(w.layout.q[z]) && j < unsigned(w.layout.q[z]) + 3)
                {
                    v.kind = 1;
                    v.index = z * 3 + j - unsigned(w.layout.q[z]);
                }
            maps.push_back(v);
        }
        m.blockOffset = append(blocks.data(), blocks.size());
        m.rowOffset = append(rows.data(), rows.size());
        m.mapOffset = append(maps.data(), maps.size());
        m.diagOffset = append(w.mdiag.data(), w.mdiag.size());
        m.lowerOffset = append(w.mlo.data(), w.mlo.size());
        m.rhsOffset = append(w.rhs.data(), w.rhs.size());
        m.scaleOffset = append(w.scale.data(), w.scale.size());
        m.workOffset = scratchBytes;
        auto a = addresses(scratchBytes, w.n, m.blocks);
        m.statusOffset = a.status;
        m.outputOffset = a.u;
        m.frameBytes = a.end - scratchBytes;
        scratchBytes = a.end;
        systems[i] = m;
        std::memcpy(bytes.data() + i * sizeof(Meta), &m, sizeof(m));
    }
};
struct ReadResult
{
    std::vector<char> bytes;
    double formation = 0, factor = 0, initial = 0, corrections = 0, total = 0, wall = 0;
};
struct Engine
{
    Context &c;
    ComPtr<ID3D12RootSignature> signature;
    ComPtr<ID3D12PipelineState> form, factor[2], initial[2], correct[2];
    guidegpu::Buffer input, work;
    ComPtr<ID3D12Resource> upload, readback, ticks;
    ComPtr<ID3D12QueryHeap> queries;
    size_t inputCapacity = 0, workCapacity = 0, usedWork = 0;
    unsigned systems = 0;
    double uploadWall = 0;
    std::vector<Meta> metadata;
    Engine(Context &, size_t inputCapacity, size_t workCapacity);
    void prepare(const Pack &);
    ReadResult run(unsigned corrections, bool mixed = false, bool formation = false, bool timestamps = true);
};
} // namespace gpuls
