#include "LaunchTests.h"
#include "TestCommon.h"
namespace gpuls {
struct Config {
  std::string name;
  Launch launch;
  unsigned corrections;
};
struct Sample {
  double f, i, c, t, w;
};
static double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, size_t(std::ceil(q * v.size()) - 1))];
}
int launchStudy(Context &c, const std::filesystem::path &inputs,
                const std::filesystem::path &out) {
  auto refs = cases(inputs, true);
  require(refs.size() == 3, "Expected declared three real systems");
  std::ostringstream raw;
  raw << std::setprecision(17)
      << "{\"warmups\":2,\"samples\":11,\"timestamped\":true,\"distribution\":"
         "\"replicated frozen source9852/14/59, round robin\",\"runs\":[";
  bool first = true;
  unsigned selectedGroup = 32, selectedTile = 100;
  auto run = [&](std::string phase, unsigned count,
                 std::vector<Config> configs) {
    Pack p(count);
    for (unsigned i = 0; i < count; i++)
      p.system(i, *refs[i % 3]->w);
    Engine e(c, p.bytes.size(), p.scratchBytes);
    e.prepare(p);
    std::vector<std::vector<Sample>> values(configs.size());
    for (int rep = -2; rep < 11; rep++) {
      for (unsigned order = 0; order < configs.size(); order++) {
        unsigned ci =
            rep % 2 == 0 ? order : unsigned(configs.size() - 1 - order);
        auto &cfg = configs[ci];
        e.launch = cfg.launch;
        auto r = e.run(cfg.corrections);
        for (unsigned i = 0; i < count; i++) {
          auto s = getStatus(r, p.systems[i]);
          require(s.code == 0 && s.done == 1 &&
                      s.corrections == cfg.corrections,
                  "Batch status");
          // Every slot is checked. Identical replica bytes inherit the
          // independent strict certificate for its first slot; nonidentical
          // slots are rescored.
          const auto &m = p.systems[i];
          const auto &firstMeta = p.systems[i % 3];
          if (i < 3 || std::memcmp(r.bytes.data() + m.outputOffset,
                                   r.bytes.data() + firstMeta.outputOffset,
                                   m.n * 8) != 0) {
            auto err = checkOutput(*refs[i % 3], r, m);
            require(err.pass, "Batch strict gate");
          }
        }
        if (rep >= 0)
          values[ci].push_back(
              {r.factor, r.initial, r.corrections, r.total, r.wall});
      }
    }
    std::vector<double> medians;
    for (unsigned ci = 0; ci < configs.size(); ci++) {
      auto &cfg = configs[ci];
      std::vector<double> times;
      for (auto &s : values[ci])
        times.push_back(s.t);
      double med = percentile(times, .5);
      medians.push_back(med);
      if (!first)
        raw << ',';
      first = false;
      unsigned tiles = (count + cfg.launch.tile - 1) / cfg.launch.tile;
      raw << "{\"phase\":\"" << phase << "\",\"name\":\"" << cfg.name
          << "\",\"systems\":" << count
          << ",\"dofsPerSystem\":183,\"group\":" << cfg.launch.group
          << ",\"tile\":" << cfg.launch.tile
          << ",\"legacy\":" << cfg.launch.legacy
          << ",\"corrections\":" << cfg.corrections
          << ",\"dispatches\":" << tiles * (2 + cfg.corrections)
          << ",\"inputBytes\":" << p.bytes.size()
          << ",\"scratchBytes\":" << p.scratchBytes
          << ",\"uploadWallMs\":" << e.uploadWall
          << ",\"medianTotalMs\":" << med
          << ",\"p95TotalMs\":" << percentile(times, .95) << ",\"raw\":[";
      for (unsigned j = 0; j < values[ci].size(); j++) {
        if (j)
          raw << ',';
        auto &s = values[ci][j];
        raw << "{\"factor\":" << s.f << ",\"initial\":" << s.i
            << ",\"correction\":" << s.c << ",\"total\":" << s.t
            << ",\"hostWaitReadbackWall\":" << s.w << '}';
      }
      raw << "]}";
      text(out / "timing.partial.json", raw.str() + "]}");
    }
    return medians;
  };
  Config b0{"B0", {1, 100, 5, true}, 5}, b1{"B1", {1, 100, 1, true}, 1};
  for (unsigned n : {1u, 10u, 100u, 2839u})
    run("baseline-correction", n, {b0, b1});
  auto packing = run(
      "group-only", 2839,
      {b1, {"C32", {32, 100, 1, false}, 1}, {"C64", {64, 100, 1, false}, 1}});
  selectedGroup = packing[1] <= packing[2] ? 32 : 64;
  ownership(c, inputs, out / "tile512-ownership", 512, selectedGroup);
  ownership(c, inputs, out / "tile2839-ownership", 2839, selectedGroup);
  auto tiles = run("tile-only", 2839,
                   {{"T100", {selectedGroup, 100, 1, false}, 1},
                    {"T512", {selectedGroup, 512, 1, false}, 1},
                    {"T2839", {selectedGroup, 2839, 1, false}, 1}});
  unsigned tileOptions[] = {100, 512, 2839};
  selectedTile =
      tileOptions[std::min_element(tiles.begin(), tiles.end()) - tiles.begin()];
  for (unsigned n : {1u, 10u, 100u, 2839u})
    run("final-paired", n,
        {b0,
         b1,
         {"best-c1", {selectedGroup, selectedTile, 1, false}, 1},
         {"best-c5", {selectedGroup, selectedTile, 5, false}, 5}});
  raw << "]}";
  text(out / "timing.json", raw.str());
  text(out / "SELECTION.json",
       "{\"group\":" + std::to_string(selectedGroup) +
           ",\"tile\":" + std::to_string(selectedTile) +
           ",\"corrections\":1,\"method\":\"lowest measured 11-sample total "
           "GPU median; staged selection\"}");
  c.audit();
  return 0;
}
} // namespace gpuls
