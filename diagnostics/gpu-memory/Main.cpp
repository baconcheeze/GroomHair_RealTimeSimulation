#include "Hybrid.h"
#include "LaunchTests.h"
#include "Layout.h"
#include "Validation.h"
#include <bit>
namespace fs = std::filesystem;
std::string array(const std::vector<double> &a) {
  std::ostringstream o;
  o << std::setprecision(17) << '[';
  for (unsigned i = 0; i < a.size(); i++) {
    if (i)
      o << ',';
    o << a[i];
  }
  return o.str() + ']';
}
void rawFile(const fs::path &p, const std::vector<char> &b) {
  fs::create_directories(p.parent_path());
  std::ofstream f(p, std::ios::binary);
  f.write(b.data(), b.size());
}
gpuls::Status status(const gpuls::ReadResult &r, const gpuls::Meta &m) {
  gpuls::Status s;
  std::memcpy(&s, r.bytes.data() + m.statusOffset, sizeof(s));
  return s;
}
std::vector<double> solution(const gpuls::ReadResult &r, const gpuls::Meta &m) {
  std::vector<double> u(m.n);
  std::memcpy(u.data(), r.bytes.data() + m.outputOffset, m.n * 8);
  return u;
}
void loadSolution(nativels::Workspace &w, const std::vector<double> &u) {
  w.u = u;
  for (unsigned j = 0; j < w.n; j++)
    w.delta[j] = u[j] / w.scale[j];
}
void cpuFormation(nativels::Workspace &w) {
  std::fill(w.mdiag.begin(), w.mdiag.end(), nativels::Mat{});
  std::fill(w.mlo.begin(), w.mlo.end(), nativels::Mat{});
  std::fill(w.rhs.begin(), w.rhs.end(), 0);
  for (auto &r : w.scaled)
    for (auto [a, x] : r.j) {
      w.rhs[a] += x * r.r;
      for (auto [b, y] : r.j) {
        unsigned sa = w.station[a], sb = w.station[b];
        if (sa == sb)
          nativels::at(w.mdiag[sa], w.slot[a], w.slot[b]) += x * y;
        else if (sa == sb + 1)
          nativels::at(w.mlo[sa], w.slot[a], w.slot[b]) += x * y;
        else if (sb != sa + 1)
          hair::require(x * y == 0, "CPU formation offband");
      }
    }
}
int replay(gpuls::Context &c, const fs::path &inputs, const fs::path &out,
           bool mixed, bool formation) {
  gpuls::Engine engine(c, 256 * 1024, 128 * 1024);
  std::vector<std::pair<unsigned, fs::path>> files;
  for (auto file : fs::directory_iterator(inputs))
    if (file.path().extension() == ".json") {
      Fixture f(file.path());
      files.push_back({f.n, file.path()});
    }
  std::sort(files.begin(), files.end());
  unsigned passes[6]{}, failures[6]{}, total = 0;
  double maxFormation = 0;
  for (auto [n, path] : files) {
    Fixture f(path);
    auto cpu = dummy(f);
    windowmath::Settings settings;
    settings.pinX = f.mx[0] < 0;
    settings.pinQ = f.mq[0] < 0;
    nativels::Workspace w(cpu, settings);
    import(w, f);
    gpuls::Pack pack(1);
    pack.system(0, w);
    double differenceFromPreserved = 0;
    if (formation) {
      auto oldDiag = w.mdiag;
      auto oldLower = w.mlo;
      auto oldRhs = w.rhs;
      cpuFormation(w);
      for (unsigned b = 0; b < w.size.size(); b++)
        for (unsigned j = 0; j < 36; j++) {
          differenceFromPreserved = std::max(
              differenceFromPreserved, std::abs(w.mdiag[b][j] - oldDiag[b][j]));
          differenceFromPreserved = std::max(
              differenceFromPreserved, std::abs(w.mlo[b][j] - oldLower[b][j]));
        }
      for (unsigned j = 0; j < w.n; j++)
        differenceFromPreserved =
            std::max(differenceFromPreserved, std::abs(w.rhs[j] - oldRhs[j]));
    }
    auto inputDiag = w.mdiag;
    auto inputLower = w.mlo;
    auto inputRHS = w.rhs;
    hair::require(w.solve(5, nativels::Mode::NativeExtended),
                  "CPU reference factor failed");
    auto reference = w.u;
    auto qr = f.bits("QRU"), qd = f.bits("QRDelta");
    engine.prepare(pack);
    std::ostringstream o;
    o << std::setprecision(17) << "{\"n\":" << n
      << ",\"inputBytes\":" << pack.bytes.size()
      << ",\"scratchBytes\":" << pack.scratchBytes
      << ",\"formedVsPreservedMaxDifference\":" << differenceFromPreserved
      << ",\"uploadWallMs\":" << engine.uploadWall
      << ",\"mixed\":" << (mixed ? "true" : "false")
      << ",\"formation\":" << (formation ? "true" : "false") << ",\"curve\":[";
    bool final = false;
    for (unsigned k : {0u, 1u, 2u, 3u, 5u}) {
      auto r = engine.run(k, mixed, formation);
      auto st = status(r, pack.systems[0]);
      auto u = solution(r, pack.systems[0]);
      loadSolution(w, u);
      auto e = error(w, qr, qd);
      double cpuDifference = 0;
      for (unsigned j = 0; j < n; j++)
        cpuDifference = std::max(cpuDifference, std::abs(u[j] - reference[j]));
      double normalResidual = 0, matrixNorm = 0, uNorm = 0, cNorm = 0;
      for (unsigned j = 0; j < n; j++) {
        double v = -w.rhs[j], rowNorm = 0;
        uNorm = std::max(uNorm, std::abs(w.u[j]));
        cNorm = std::max(cNorm, std::abs(w.rhs[j]));
        for (unsigned z = 0; z < n; z++) {
          double a = w.m(j, z);
          v += a * w.u[z];
          rowNorm += std::abs(a);
        }
        normalResidual = std::max(normalResidual, std::abs(v));
        matrixNorm = std::max(matrixNorm, rowNorm);
      }
      double backward =
          normalResidual / std::max(1e-300, matrixNorm * uNorm + cNorm);
      auto work = gpuls::addresses(0, n, unsigned(w.size.size()));
      std::vector<double> sizes(5);
      std::memcpy(sizes.data(), r.bytes.data() + work.steps, 40);
      bool finiteOutput = std::all_of(
          u.begin(), u.end(), [](double v) { return std::isfinite(v); });
      bool valid = finiteOutput && st.code == 0 && st.done == 1 &&
                   st.corrections == k && e.pass;
      passes[k] += valid;
      failures[k] += st.code != 0;
      if (k)
        o << ',';
      o << "{\"corrections\":" << k
        << ",\"pass\":" << (valid ? "true" : "false") << ",\"code\":" << st.code
        << ",\"stage\":" << st.stage << ",\"index\":" << st.index
        << ",\"lost\":" << st.lost << ",\"stagnation\":" << st.stagnation
        << ",\"positionError\":" << e.pos << ",\"rawAngleError\":" << e.angle
        << ",\"scaledRelative\":" << e.relative
        << ",\"normalBackward\":" << backward
        << ",\"normalResidualAbs\":" << normalResidual
        << ",\"correctionMaxScaledComponent\":" << array(sizes)
        << ",\"LSstationarity\":" << e.stationarity
        << ",\"maxScaledDifferenceFromNativeExtended\":" << cpuDifference
        << ",\"gpuMs\":{\"formation\":" << r.formation
        << ",\"factor\":" << r.factor << ",\"initial\":" << r.initial
        << ",\"corrections\":" << r.corrections << ",\"total\":" << r.total
        << "},\"hostWaitAndReadbackMs\":" << r.wall << ",\"u\":" << array(w.u)
        << ",\"delta\":" << array(w.delta) << '}';
      if (formation && st.code == 0) {
        auto a = gpuls::addresses(0, n, unsigned(w.size.size()));
        for (unsigned b = 0; b < w.size.size(); b++)
          for (unsigned j = 0; j < 36; j++) {
            double vd, vl;
            std::memcpy(&vd, r.bytes.data() + a.diag + b * 288 + j * 8, 8);
            std::memcpy(&vl, r.bytes.data() + a.lower + b * 288 + j * 8, 8);
            maxFormation =
                std::max(maxFormation, std::abs(vd - inputDiag[b][j]));
            maxFormation =
                std::max(maxFormation, std::abs(vl - inputLower[b][j]));
          }
        for (unsigned j = 0; j < n; j++) {
          double v;
          std::memcpy(&v, r.bytes.data() + a.rhs + j * 8, 8);
          maxFormation = std::max(maxFormation, std::abs(v - inputRHS[j]));
        }
      }
      if (k == 5) {
        final = valid;
        if (!valid) {
          rawFile(out / path.stem() / "input.bin", pack.bytes);
          rawFile(out / path.stem() / "workspace.bin", r.bytes);
        }
      }
    }
    o << "]}";
    gpuls::text(out / path.filename(), o.str());
    total++;
    if (!final && !mixed) {
      gpuls::text(out / "FAILURE.txt",
                  path.filename().string() +
                      " failed within five corrections; expansion stopped");
      return 1;
    }
  }
  std::ostringstream o;
  o << std::setprecision(17) << "{\"cases\":" << total
    << ",\"maxFormationDifference\":" << maxFormation << ",\"budgets\":[";
  bool first = true;
  for (unsigned k : {0u, 1u, 2u, 3u, 5u}) {
    if (!first)
      o << ',';
    first = false;
    o << "{\"corrections\":" << k << ",\"pass\":" << passes[k]
      << ",\"factorOrSolveFailures\":" << failures[k] << '}';
  }
  o << "]}";
  gpuls::text(out / "STATUS.json", o.str());
  std::cout << o.str() << '\n';
  c.audit();
  return passes[5] == total && maxFormation < 1e-13 ? 0 : 1;
}
int main(int argc, char **argv) {
  try {
    hair::require(argc >= 4 && argc <= 7,
                  "G64MemoryCandidate "
                  "replay|formation|mixed|hybrid5|hybrid1|dynamics|benchmark "
                  "inputs output");
    if (argc >= 5) {
      std::string layout = argv[4];
      gpuls::activeLayout = layout == "baseline"      ? 0
                            : layout == "work32"      ? 1
                            : layout == "workinput32" ? 2
                                                      : 99;
      hair::require(gpuls::activeLayout < 3,
                    "Choose baseline/work32/workinput32");
    }
    if (argc >= 6)
      gpuls::activeLaunch.corrections = unsigned(std::stoul(argv[5]));
    gpuls::Context c;
    gpuls::text(fs::path(argv[3]) / "device.json", c.json());
    if (!c.fp64)
      return 3;
    std::string mode = argv[1];
    if (mode == "codec")
      return gpuls::codec(argv[2], argv[3]);
    if (mode == "replay" || mode == "formation")
      return replay(c, argv[2], argv[3], mode == "mixed", mode == "formation");
    if (mode == "hybrid5" || mode == "hybrid1")
      return gpuls::hybridSuite(c, argv[2], argv[3], mode == "hybrid5" ? 5 : 1);
    if (mode == "dynamics") {
      int code = gpuls::hybridDynamics(c, argv[2], argv[3]);
      c.audit();
      return code;
    }
    if (mode == "benchmark")
      return gpuls::launchStudy(c, argv[2], argv[3]);
    if (mode == "ownership")
      return gpuls::ownership(c, argv[2], argv[3], 2839, 32);
    throw std::runtime_error("Unknown mode");
  } catch (const std::exception &e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return 2;
  }
}
