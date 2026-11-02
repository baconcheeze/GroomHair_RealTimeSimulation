#include "LaunchTests.h"
#include "TestCommon.h"
namespace gpuls {
int ownership(Context &c, const std::filesystem::path &inputs,
              const std::filesystem::path &out, unsigned tile,
              unsigned onlyGroup) {
  auto refs = cases(inputs);
  std::ostringstream log;
  log << std::setprecision(17) << "{\"tile\":" << tile << ",\"runs\":[";
  bool first = true;
  unsigned runs = 0, slots = 0;
  std::vector<unsigned> counts =
      tile == 100
          ? std::vector<unsigned>{1, 31, 32, 33, 63, 64, 65, 99, 100, 101}
          : std::vector<unsigned>{101, 511, 512, 513, 2839};
  size_t inputMax = 0, workMax = 0;
  for (auto &f : refs) {
    Pack test(1);
    test.system(0, *f->w);
    inputMax = std::max(inputMax, test.bytes.size());
    workMax = std::max(workMax, size_t(test.scratchBytes));
  }
  unsigned maxCount = *std::max_element(counts.begin(), counts.end()) + 6;
  Engine e(c, maxCount * inputMax, 64 + maxCount * (workMax + 64));
  // The unchanged legacy shader supplies the launch-control result.
  Pack controlPack(unsigned(refs.size()));
  for (unsigned i = 0; i < refs.size(); ++i)
    controlPack.system(i, *refs[i]->w);
  e.launch = {1, 100, 1, true};
  e.prepare(controlPack);
  std::vector<std::vector<double>> control1(refs.size()), control5(refs.size());
  for (unsigned k : {1u, 5u}) {
    auto result = e.run(k);
    for (unsigned i = 0; i < refs.size(); ++i) {
      require(getStatus(result, controlPack.systems[i]).code == 0 &&
                  checkOutput(*refs[i], result, controlPack.systems[i]).pass,
              "Legacy control strict gate");
      (k == 1 ? control1 : control5)[i] = refs[i]->w->u;
    }
  }
  for (unsigned group : {1u, 32u, 64u}) {
    if (onlyGroup && group != onlyGroup)
      continue;
    for (unsigned count : counts)
      for (bool invalid : {false, true}) {
        unsigned total = count + 6;
        Pack p(total);
        p.scratchBytes = 64;
        std::vector<unsigned> selection;
        for (unsigned i = 0; i < total; i++) {
          unsigned pick = (i * 17 + count) % refs.size();
          selection.push_back(pick);
          p.system(i, *refs[pick]->w);
          p.scratchBytes += 64;
        }
        unsigned bad = 3 + count / 2;
        if (invalid) {
          double neg = -1;
          unsigned block = 0;
          while (refs[selection[bad]]->w->size[block] == 0)
            ++block;
          std::memcpy(p.bytes.data() + p.systems[bad].diagOffset + block * 288,
                      &neg, 8);
        }
        e.launch = {group, tile, 1, false};
        e.ownershipCheck = true;
        e.rangeBase = 3;
        e.rangeCount = count;
        e.prepare(p);
        std::vector<char> seed(p.scratchBytes, char(0xa5));
        for (auto &m : p.systems) {
          uint32_t zero = 0;
          std::memcpy(seed.data() + m.statusOffset + 28, &zero, 4);
        }
        e.seed(seed);
        unsigned k = count == 101 ? 5 : 1;
        auto r = e.run(k);
        std::vector<bool> allowed(seed.size(), false);
        double maxPos = 0, maxAngle = 0, maxControl = 0;
        unsigned bitwiseSlots = 0;
        for (unsigned i = 0; i < total; i++) {
          auto &m = p.systems[i];
          if (i >= 3 && i < 3 + count) {
            std::fill(allowed.begin() + m.workOffset,
                      allowed.begin() + m.workOffset + m.frameBytes, true);
            auto s = getStatus(r, m);
            if (invalid && i == bad) {
              require(s.code != 0 && s.pad == 1 && s.corrections == 0,
                      "Invalid sticky ownership");
            } else {
              require(s.code == 0 && s.done == 1 && s.corrections == k &&
                          s.pad == 1 + 256 + k * 65536,
                      "Duplicate/missing stage ownership");
              auto err = checkOutput(*refs[selection[i]], r, m);
              require(err.pass, "Ownership strict accuracy");
              auto &ref = *refs[selection[i]];
              auto &baseline = (k == 1 ? control1 : control5)[selection[i]];
              std::vector<double> baselineDelta(m.n);
              for (unsigned j = 0; j < m.n; ++j) {
                baselineDelta[j] = baseline[j] / ref.w->scale[j];
                maxControl =
                    std::max(maxControl, std::abs(ref.w->u[j] - baseline[j]));
              }
              require(error(*ref.w, baseline, baselineDelta).pass,
                      "Original G64 comparison strict gate");
              bitwiseSlots +=
                  std::memcmp(ref.w->u.data(), baseline.data(), m.n * 8) == 0;

              maxPos = std::max(maxPos, err.pos);
              maxAngle = std::max(maxAngle, err.angle);
              slots++;
            }
          }
        }
        for (size_t b = 0; b < seed.size(); b++)
          if (!allowed[b])
            require(seed[b] == r.bytes[b], "Guard or neighbor modified");
        if (!first)
          log << ',';
        first = false;
        log << "{\"group\":" << group << ",\"count\":" << count
            << ",\"base\":3,\"corrections\":" << k << ",\"invalid\":" << invalid
            << ",\"guardBytesChecked\":"
            << std::count(allowed.begin(), allowed.end(), false)
            << ",\"maxPositionError\":" << maxPos
            << ",\"maxAngleError\":" << maxAngle
            << ",\"maxControlScaledDifference\":" << maxControl
            << ",\"bitwiseControlSlots\":" << bitwiseSlots << ",\"pass\":true}";
        runs++;
        text(out / "ownership.partial.json", log.str() + "]}");
      }
  }
  log << "],\"runsPassed\":" << runs << ",\"validSlots\":" << slots << "}";
  text(out / "ownership.json", log.str());
  c.audit();
  return 0;
}
} // namespace gpuls
