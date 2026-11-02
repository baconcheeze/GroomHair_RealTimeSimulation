#pragma once
#include <DirectXMath.h>
#include <filesystem>
#include <vector>
#include <string>
#include <cstdint>
#include <array>
#include <cfloat>
using Float3 = DirectX::XMFLOAT3;
static_assert(sizeof(Float3)==12);
static_assert(sizeof(uint32_t)==4);
struct Bounds {
    Float3 min{FLT_MAX,FLT_MAX,FLT_MAX},max{-FLT_MAX,-FLT_MAX,-FLT_MAX};
    void add(Float3 p);
    Float3 center() const;
    float radius() const;
};
struct Strands {
    std::vector<Float3> positions;
    std::vector<uint32_t> offsets,sourceIds,segments;
    Bounds bounds;
    void derive();
    std::string diagnostics() const;
};
struct HeadVertex { Float3 position,normal; };
static_assert(sizeof(HeadVertex)==24);
struct MeshData { std::vector<HeadVertex> vertices; std::vector<uint32_t> indices; Bounds bounds; };
struct HullData {
    int32_t joint{}; uint32_t id{};
    std::array<float,16> sourceWorld{},sourceNeck{};
    std::vector<Float3> sourcePoints,sourceLocal,neckPositions;
    std::vector<std::array<float,4>> planes;
    std::vector<uint32_t> triangles;
};
// Only raw Jolt positions pass through this reflection. Baked Unity mesh never does.
inline Float3 joltToNative(Float3 p) { return {p.x,p.y,-p.z}; }
// Already-converted native coordinates. Direction only: no translation or second Z reflection.
inline Float3 worldVectorToHair(Float3 world,const DirectX::XMFLOAT4X4& worldToHairRotation){
    Float3 local;DirectX::XMStoreFloat3(&local,DirectX::XMVector3TransformNormal(DirectX::XMLoadFloat3(&world),DirectX::XMLoadFloat4x4(&worldToHairRotation)));return local;
}
struct RestData {
    std::string dataset;
    Strands guide,render;
    MeshData selectedHead,originalHead;
    std::vector<HullData> hulls;
    std::vector<Float3> hullPositions;
    std::vector<uint32_t> hullEdges;
    DirectX::XMFLOAT4X4 cameraBasis{};
    // Fixed frame-0 neck pose from staged JHHD metadata; independent of the orbit camera.
    DirectX::XMFLOAT4X4 hairLocalToWorld{},worldToHairRotation{};
    Bounds combined;
    void load(const std::filesystem::path& assets);
    void report(const std::filesystem::path& path,unsigned frames,bool gpuInitial,bool gpuFinal) const;
};
