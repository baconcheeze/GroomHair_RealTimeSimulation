#pragma once
#include "Layout.h"
#include "Validation.h"
#include <memory>
namespace gpuls {
struct Case {
  Fixture f;
  GuideCpuState cpu;
  windowmath::Settings settings;
  std::unique_ptr<nativels::Workspace> w;
  std::vector<double> u, delta, qr, qd;
  Case(const std::filesystem::path &p) : f(p), cpu(dummy(f)) {
    settings.pinX = f.mx[0] < 0;
    settings.pinQ = f.mq[0] < 0;
    w = std::make_unique<nativels::Workspace>(cpu, settings);
    import(*w, f);
    qr = f.bits("QRU");
    qd = f.bits("QRDelta");
    require(w->solve(5, nativels::Mode::NativeExtended), "CPU fixture factor");
    u = w->u;
    delta = w->delta;
  }
};
inline std::vector<std::unique_ptr<Case>> cases(const std::filesystem::path &p,
                                                bool real = false) {
  std::vector<std::filesystem::path> files;
  for (auto &f : std::filesystem::directory_iterator(p))
    if (f.path().extension() == ".json" &&
        (!real || f.path().filename() == "primary__original_initial.json" ||
         f.path().filename() == "heldout-slot0__original_initial.json" ||
         f.path().filename() == "heldout-slot2__original_initial.json"))
      files.push_back(f.path());
  std::sort(files.begin(), files.end());
  // Match the pilot exactly: primary9852, source14, source59.
  if (real && files.size() == 3)
    std::rotate(files.begin(), files.end() - 1, files.end());
  std::vector<std::unique_ptr<Case>> result;
  for (auto &f : files)
    result.push_back(std::make_unique<Case>(f));
  require(!result.empty(), "No fixtures");
  return result;
}
inline Status getStatus(const ReadResult &r, const Meta &m) {
  Status s;
  std::memcpy(&s, r.bytes.data() + m.statusOffset, sizeof(s));
  return s;
}
inline Error checkOutput(Case &f, const ReadResult &r, const Meta &m) {
  auto &w = *f.w;
  w.u.resize(m.n);
  std::memcpy(w.u.data(), r.bytes.data() + m.outputOffset, m.n * 8);
  for (unsigned j = 0; j < m.n; j++) {
    require(std::isfinite(w.u[j]), "Nonfinite output");
    w.delta[j] = w.u[j] / w.scale[j];
  }
  return error(w, f.qr, f.qd);
}
} // namespace gpuls
