#include "Interleave.h"
#include "LaunchTests.h"
#include "TestCommon.h"
#include <bit>
namespace gpuls {
int codec(const fs::path &inputs, const fs::path &out) {
  auto refs = cases(inputs);
  unsigned tested = 0;
  std::ostringstream log;
  log << "{\"tests\":[";
  bool first = true;
  for (unsigned count : {1u, 31u, 32u, 33u, 63u, 64u, 65u, 99u, 100u, 101u,
                         511u, 512u, 513u, 2839u}) {
    Pack p(count);
    p.scratchBytes = 64;
    for (unsigned i = 0; i < count; i++) {
      p.system(i, *refs[(i * 17 + count) % refs.size()]->w);
      p.scratchBytes += 64;
    }
    for (unsigned mode : {1u, 2u}) {
      Wire original(p, mode);
      require(original.inputRoundTrip(p.bytes.size()) == p.bytes,
              "Finite input round trip");
      Pack bits = p;
      uint64_t patterns[] = {0x0000000000000000ull,
                             0x8000000000000000ull,
                             0x7ff0000000000001ull,
                             0xfff0000000000000ull,
                             1ull,
                             0x3ff123456789abcdull};
      unsigned k = 0;
      for (auto &map : original.maps)
        for (auto &s : map.input)
          for (unsigned j = 0; j < s.count; j++) {
            uint64_t value = patterns[k++ % 6];
            std::memcpy(bits.bytes.data() + s.canonical + j * 8, &value, 8);
          }
      Wire encoded(bits, mode);
      require(encoded.inputRoundTrip(bits.bytes.size()) == bits.bytes,
              "Exact arbitrary FP64 input bits round trip");
      std::vector<char> seed(p.scratchBytes);
      for (size_t b = 0; b < seed.size(); b++)
        seed[b] = char((b * 37 + 19) & 255);
      auto physical = encoded.encodeWork(seed);
      require(encoded.decodeWork(physical, seed) == seed,
              "Exact work/status/boundary round trip");
      encoded.guards(physical, physical);
      if (!first)
        log << ',';
      first = false;
      log << "{\"systems\":" << count << ",\"layout\":" << mode
          << ",\"physicalInput\":" << encoded.input.size()
          << ",\"physicalScratch\":" << encoded.workBytes
          << ",\"canonicalInput\":" << p.bytes.size()
          << ",\"canonicalScratch\":" << p.scratchBytes << ",\"pass\":true}";
      tested++;
    }
  }
  Pack overflow(1);
  overflow.system(0, *refs[0]->w);
  overflow.systems[0].blocks = UINT32_MAX;
  bool rejected = false;
  try {
    Wire w(overflow, 1);
  } catch (const std::exception &) {
    rejected = true;
  }
  require(rejected, "Overflow allocation must fail");
  log << "],\"passed\":" << tested << ",\"overflowRejected\":true}";
  text(out / "codec.json", log.str());
  auto real = cases(inputs, true);
  Pack p(34);
  for (unsigned i = 0; i < 34; i++)
    p.system(i, *real[i % 3]->w);
  Wire w(p, 1), b(p, 2);
  std::ofstream csv(out / "addresses.csv");
  csv << "system,quantity,baseline,work32,workinput32\n";
  for (unsigned i = 0; i < 34; i++) {
    auto &m = p.systems[i];
    auto a = addresses(m.workOffset, m.n, m.blocks);
    auto inp = [&](uint32_t ptr) {
      for (auto &s : b.maps[i].input)
        if (ptr >= s.canonical && ptr < s.canonical + s.count * 8)
          return Wire::physical(b.tiles[i / 32].inputBase,
                                s.plane + ptr - s.canonical, i % 32);
      throw std::runtime_error("Unmapped input trace");
    };
    auto work = [&](uint32_t ptr) {
      for (auto &s : w.maps[i].work)
        if (ptr >= s.canonical && ptr < s.canonical + s.count * 8)
          return Wire::physical(w.tiles[i / 32].workBase,
                                s.plane + ptr - s.canonical, i % 32);
      throw std::runtime_error("Unmapped work trace");
    };
    for (auto item : std::vector<std::pair<std::string, uint32_t>>{
             {"Factor.Mdiag", m.diagOffset},
             {"Initial.rhs", m.rhsOffset},
             {"Correct.rowB", m.rowOffset + 32},
             {"Correct.rowA0", m.rowOffset + 40}})
      csv << i << ',' << item.first << ',' << item.second << ',' << item.second
          << ',' << inp(item.second) << '\n';
    for (auto item :
         std::vector<std::pair<std::string, uint32_t>>{{"Factor.L", a.L},
                                                       {"Solve.C", a.C},
                                                       {"Solve.y", a.y},
                                                       {"Solve.du", a.du},
                                                       {"Correct.u", a.u},
                                                       {"Correct.s", a.s}})
      csv << i << ',' << item.first << ',' << item.second << ','
          << work(item.second) << ',' << work(item.second) << '\n';
    for (auto item : std::vector<std::pair<std::string, uint32_t>>{
             {"integer.meta", i * 64},
             {"integer.rowCount", m.rowOffset},
             {"integer.rowColumn", m.rowOffset + 8},
             {"integer.block", m.blockOffset},
             {"integer.map", m.mapOffset}})
      csv << i << ',' << item.first << ',' << item.second << ',' << item.second
          << ',' << item.second << '\n';
    csv << i << ",integer.status," << m.statusOffset << ','
        << w.tiles[i / 32].statusBase + (i % 32) * 32 << ','
        << b.tiles[i / 32].statusBase + (i % 32) * 32 << '\n';
  }
  return 0;
}
} // namespace gpuls
