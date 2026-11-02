#include "Interleave.h"
#include "LaunchTests.h"
#include "TestCommon.h"
namespace gpuls {
struct Sample {
  double factor, initial, correction, total, host, unpack, packing, upload,
      integrated;
};
static double percentile(std::vector<double> v, double q) {
  std::sort(v.begin(), v.end());
  return v[std::min(v.size() - 1, size_t(std::ceil(q * v.size()) - 1))];
}
int launchStudy(Context &c, const fs::path &inputs, const fs::path &out) {
  auto refs = cases(inputs, true);
  require(refs.size() == 3, "Original corpus order");
  std::ostringstream log;
  log << std::setprecision(17)
      << "{\"group\":32,\"tile\":2839,\"warmup\":2,\"primarySamples\":11,"
         "\"order\":\"source9852/source14/source59 round robin\",\"runs\":[";
  bool first = true;
  auto run = [&](std::string phase, unsigned count, std::vector<unsigned> modes,
                 unsigned corrections, unsigned samples, bool fresh,
                 bool reversed) {
    Pack p(count);
    for (unsigned i = 0; i < count; i++)
      p.system(i, *refs[i % 3]->w);
    std::vector<std::unique_ptr<Engine>> engines;
    std::vector<std::string> memory;
    std::vector<double> prepPack, prepUpload;
    for (auto mode : modes) {
      auto e = std::make_unique<Engine>(c, p.bytes.size(), p.scratchBytes);
      e->layout = mode;
      e->prepare(p);
      memory.push_back(e->memoryJSON());
      prepPack.push_back(e->packingWall);
      prepUpload.push_back(e->uploadWall);
      engines.push_back(std::move(e));
    }
    std::vector<std::vector<Sample>> times(modes.size());
    for (int rep = -2; rep < int(samples); rep++)
      for (unsigned order = 0; order < modes.size(); order++) {
        bool flip = (rep % 2 != 0) ^ reversed;
        unsigned ci = flip ? unsigned(modes.size() - 1 - order) : order;
        auto &e = *engines[ci];
        auto start = std::chrono::steady_clock::now();
        if (fresh)
          e.prepare(p);
        auto r = e.run(corrections);
        double wall = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
        for (unsigned i = 0; i < count; i++) {
          auto &m = p.systems[i];
          auto s = getStatus(r, m);
          require(s.code == 0 && s.done == 1 && s.corrections == corrections,
                  "Timing status");
          if (i < 3 ||
              std::memcmp(r.bytes.data() + m.outputOffset,
                          r.bytes.data() + p.systems[i % 3].outputOffset,
                          m.n * 8) != 0)
            require(checkOutput(*refs[i % 3], r, m).pass, "Timing strict gate");
        }
        if (rep >= 0)
          times[ci].push_back({r.factor, r.initial, r.corrections, r.total,
                               r.wall, r.unpacking, fresh ? e.packingWall : 0,
                               fresh ? e.uploadWall : 0, wall});
      }
    std::vector<double> med;
    for (unsigned ci = 0; ci < modes.size(); ci++) {
      std::vector<double> total, integrated;
      for (auto &s : times[ci]) {
        total.push_back(s.total);
        integrated.push_back(s.integrated);
      }
      med.push_back(percentile(total, .5));
      if (!first)
        log << ',';
      first = false;
      log << "{\"phase\":\"" << phase << "\",\"systems\":" << count
          << ",\"layout\":" << modes[ci] << ",\"corrections\":" << corrections
          << ",\"samples\":" << samples << ",\"freshInput\":" << fresh
          << ",\"reverseStart\":" << reversed
          << ",\"medianTotal\":" << med.back()
          << ",\"p95Total\":" << percentile(total, .95)
          << ",\"medianIntegratedWall\":" << percentile(integrated, .5)
          << ",\"preparePackOnce\":" << prepPack[ci]
          << ",\"prepareUploadOnce\":" << prepUpload[ci]
          << ",\"memory\":" << memory[ci] << ",\"raw\":[";
      for (unsigned j = 0; j < times[ci].size(); j++) {
        if (j)
          log << ',';
        auto &s = times[ci][j];
        log << "{\"factor\":" << s.factor << ",\"initial\":" << s.initial
            << ",\"correction\":" << s.correction << ",\"total\":" << s.total
            << ",\"hostReadbackWait\":" << s.host << ",\"unpack\":" << s.unpack
            << ",\"packing\":" << s.packing << ",\"upload\":" << s.upload
            << ",\"integratedWall\":" << s.integrated << '}';
      }
      log << "]}";
      text(out / "timing.partial.json", log.str() + "]}");
    }
    return med;
  };
  unsigned best = 1;
  for (unsigned n : {1u, 10u, 100u, 2839u}) {
    auto med = run("resident-c1", n, {0, 1, 2}, 1, 11, false, false);
    if (n == 2839)
      best = med[1] <= med[2] ? 1 : 2;
  }
  run("reverse-resident-c1", 2839, {0, best}, 1, 7, false, true);
  for (unsigned n : {1u, 10u, 100u, 2839u})
    run("resident-c5", n, {0, best}, 5, 7, false, false);
  for (unsigned n : {1u, 10u, 100u, 2839u})
    run("fresh-input-c1", n, {0, 1, 2}, 1, 7, true, false);
  run("reverse-fresh-input-c1", 2839, {0, best}, 1, 7, true, true);
  log << "]}";
  text(out / "timing.json", log.str());
  text(out / "SELECTION.json",
       "{\"accuracyCandidate\":" + std::to_string(best) +
           ",\"criterion\":\"lowest resident c1 2839 median; recommendation "
           "requires reverse pass and integrated-path evaluation\"}");
  c.audit();
  return 0;
}
} // namespace gpuls
