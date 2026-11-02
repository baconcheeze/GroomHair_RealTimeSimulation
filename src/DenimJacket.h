#pragma once
#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <vector>
struct DenimJacketAsset {
    struct Position { float x,y,z; };
    struct Color { float r,g,b,a; };
    struct Surface { float uv[2]; uint32_t texture,flags; float normal[4]; uint32_t joints[4]; float weights[4]; };
    static_assert(sizeof(Position)==12 && sizeof(Color)==16 && sizeof(Surface)==64);
    static_assert(offsetof(Surface,joints)==32 && offsetof(Surface,weights)==48);
    std::vector<Position> positions;
    std::vector<Color> colors;
    std::vector<Surface> surfaces;
    std::vector<uint32_t> indices;
    static DenimJacketAsset Load(const std::filesystem::path& path,uint32_t jointCount);
};
