#pragma once
#include "Layout.h"
#include <array>
namespace gpuls {
struct Tile {
  uint32_t workBase, statusBase, blocks, n, inputBase, rows, pad0 = 0, pad1 = 0;
};
static_assert(sizeof(Tile) == 32 && offsetof(Tile, inputBase) == 16);
struct Span {
  uint32_t canonical, plane, count;
}; // count of exact eight-byte values
struct SystemMap {
  std::array<Span, 10> work;
  std::vector<Span> input;
};
struct Wire {
  std::vector<char> input;
  std::vector<Meta> canonical;
  std::vector<Tile> tiles;
  std::vector<SystemMap> maps;
  uint32_t workBytes = 0, canonicalBytes = 0;
  uint64_t numericWork = 0, numericInput = 0;
  unsigned variant;
  static uint32_t checked(uint64_t x) {
    require(x < UINT32_MAX, "Raw GPU address exceeds uint32 limit");
    return uint32_t(x);
  }
  static uint32_t reserve(uint64_t &cursor, uint64_t bytes) {
    uint64_t start = (cursor + 63) & ~uint64_t(63);
    cursor = start + bytes;
    checked(cursor);
    return checked(start);
  }
  static std::array<Span, 10> workSpans(const Meta &m, const Tile &t) {
    auto a = addresses(m.workOffset, m.n, m.blocks);
    uint32_t c = 0;
    std::array<Span, 10> s;
    unsigned k = 0;
    auto add = [&](uint32_t old, uint32_t count, uint32_t capacity) {
      s[k++] = {old, c, count};
      c += capacity * 8;
    };
    add(a.L, m.blocks * 36, t.blocks * 36);
    add(a.C, m.blocks * 36, t.blocks * 36);
    add(a.u, m.n, t.n);
    add(a.y, m.n, t.n);
    add(a.du, m.n, t.n);
    add(a.s, m.n, t.n);
    add(a.steps, 5, 5);
    add(a.diag, m.blocks * 36, t.blocks * 36);
    add(a.lower, m.blocks * 36, t.blocks * 36);
    add(a.rhs, m.n, t.n);
    return s;
  }
  static std::vector<Span> inputSpans(const Meta &m, const Tile &t) {
    std::vector<Span> s;
    uint32_t c = 0;
    auto add = [&](uint32_t old, uint32_t n, uint32_t capacity) {
      s.push_back({old, c, n});
      c += capacity * 8;
    };
    add(m.diagOffset, m.blocks * 36, t.blocks * 36);
    add(m.lowerOffset, m.blocks * 36, t.blocks * 36);
    add(m.rhsOffset, m.n, t.n);
    add(m.scaleOffset, m.n, t.n);
    for (unsigned row = 0; row < m.rows; row++)
      s.push_back({m.rowOffset + row * 88 + 32, c + row * 56, 7});
    return s;
  }
  Wire(const Pack &p, unsigned mode)
      : input(p.bytes), canonical(p.systems), canonicalBytes(p.scratchBytes),
        variant(mode) {
    require(mode == 1 || mode == 2, "Interleave mode");
    maps.resize(p.systems.size());
    tiles.resize((p.systems.size() + 31) / 32);
    uint64_t wc = 64, ic = input.size();
    uint32_t descriptor = reserve(ic, tiles.size() * sizeof(Tile));
    input.resize(checked(ic));
    for (unsigned ti = 0; ti < tiles.size(); ti++) {
      auto &t = tiles[ti];
      for (unsigned lane = 0; lane < 32 && ti * 32 + lane < canonical.size();
           lane++) {
        auto &m = canonical[ti * 32 + lane];
        t.blocks = std::max(t.blocks, m.blocks);
        t.n = std::max(t.n, m.n);
        t.rows = std::max(t.rows, m.rows);
      }
      uint64_t workSlots = uint64_t(t.blocks) * 144 + uint64_t(t.n) * 5 + 5;
      t.workBase = reserve(wc, workSlots * 32 * 8);
      wc += 64;
      t.statusBase = reserve(wc, 32 * 32);
      wc += 64;
      if (mode == 2) {
        uint64_t inputSlots =
            uint64_t(t.blocks) * 72 + uint64_t(t.n) * 2 + uint64_t(t.rows) * 7;
        t.inputBase = reserve(ic, inputSlots * 32 * 8);
        ic += 64;
        input.resize(checked(ic));
      }
      for (unsigned lane = 0; lane < 32 && ti * 32 + lane < canonical.size();
           lane++) {
        unsigned id = ti * 32 + lane;
        auto m = canonical[id];
        maps[id].work = workSpans(m, t);
        maps[id].input = inputSpans(m, t);
        for (auto &s : maps[id].work)
          numericWork += uint64_t(s.count) * 8;
        for (auto &s : maps[id].input) {
          numericInput += uint64_t(s.count) * 8;
          if (mode == 2) {
            for (unsigned j = 0; j < s.count; j++)
              std::memcpy(input.data() +
                              physical(t.inputBase, s.plane + j * 8, lane),
                          p.bytes.data() + s.canonical + j * 8, 8);
            std::memset(input.data() + s.canonical, 0, s.count * 8);
          }
        }
        m.pad0 = descriptor + ti * sizeof(Tile);
        std::memcpy(input.data() + id * sizeof(Meta), &m, sizeof(m));
      }
    }
    workBytes = checked(wc);
    for (unsigned ti = 0; ti < tiles.size(); ti++)
      std::memcpy(input.data() + descriptor + ti * sizeof(Tile), &tiles[ti],
                  sizeof(Tile));
  }
  static uint32_t physical(uint32_t base, uint32_t virtualByte, unsigned lane) {
    return checked(uint64_t(base) + uint64_t(virtualByte) * 32 +
                   uint64_t(lane) * 8);
  }
  std::vector<char> inputRoundTrip(size_t length) const {
    require(length <= input.size(), "Input length");
    std::vector<char> out(input.begin(), input.begin() + length);
    for (unsigned id = 0; id < canonical.size(); id++) {
      std::memcpy(out.data() + id * sizeof(Meta), &canonical[id], sizeof(Meta));
      if (variant == 2)
        for (auto &s : maps[id].input)
          for (unsigned j = 0; j < s.count; j++)
            std::memcpy(out.data() + s.canonical + j * 8,
                        input.data() + physical(tiles[id / 32].inputBase,
                                                s.plane + j * 8, id % 32),
                        8);
    }
    return out;
  }
  std::vector<char> encodeWork(const std::vector<char> &src) const {
    require(src.size() == canonicalBytes, "Canonical seed size");
    std::vector<char> out(workBytes, char(0xa5));
    for (unsigned id = 0; id < canonical.size(); id++) {
      auto &t = tiles[id / 32];
      for (auto &s : maps[id].work)
        for (unsigned j = 0; j < s.count; j++)
          std::memcpy(out.data() +
                          physical(t.workBase, s.plane + j * 8, id % 32),
                      src.data() + s.canonical + j * 8, 8);
      std::memcpy(out.data() + t.statusBase + (id % 32) * 32,
                  src.data() + canonical[id].statusOffset, 32);
    }
    return out;
  }
  std::vector<char> decodeWork(const std::vector<char> &src,
                               const std::vector<char> &seed = {}) const {
    require(src.size() == workBytes, "Physical readback size");
    std::vector<char> out =
        seed.empty() ? std::vector<char>(canonicalBytes) : seed;
    for (unsigned id = 0; id < canonical.size(); id++) {
      auto &t = tiles[id / 32];
      for (auto &s : maps[id].work)
        for (unsigned j = 0; j < s.count; j++)
          std::memcpy(
              out.data() + s.canonical + j * 8,
              src.data() + physical(t.workBase, s.plane + j * 8, id % 32), 8);
      std::memcpy(out.data() + canonical[id].statusOffset,
                  src.data() + t.statusBase + (id % 32) * 32, 32);
    }
    return out;
  }
  void guards(const std::vector<char> &raw,
              const std::vector<char> &expected) const {
    require(expected.size() == raw.size(), "Guard seed");
    std::vector<bool> used(workBytes, false);
    for (unsigned id = 0; id < canonical.size(); id++) {
      auto &t = tiles[id / 32];
      for (auto &s : maps[id].work)
        for (unsigned j = 0; j < s.count; j++) {
          auto p = physical(t.workBase, s.plane + j * 8, id % 32);
          for (unsigned b = 0; b < 8; b++)
            used[p + b] = true;
        }
      auto st = t.statusBase + (id % 32) * 32;
      for (unsigned b = 0; b < 32; b++)
        used[st + b] = true;
    }
    for (unsigned b = 0; b < workBytes; b++)
      if (!used[b])
        require(raw[b] == expected[b], "Physical padding/guard corruption");
  }
};
} // namespace gpuls
