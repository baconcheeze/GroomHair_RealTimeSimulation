#include "../joint/Problem.h"
#include "Hybrid.h"
namespace gpuls
{
int hybridSuite(Context &c, const fs::path &input, const fs::path &out, unsigned count)
{
    Engine engine(c, 256 * 1024, 128 * 1024);
    hybridEngine = &engine;
    hybridRecords.clear();
    unsigned cases = 0, passed = 0;
    for (auto file : fs::directory_iterator(input))
    {
        if (file.path().extension() != ".jwp")
            continue;
        auto p = jointcpu::loadProblem(file.path());
        auto name = file.path().stem().string();
        auto state = p->initial;
        nativels::Workspace w(p->cpu, p->set);
        std::vector<std::pair<double, double>> tolerances =
            name == "primary" ? std::vector<std::pair<double, double>>{{1e-11, 1e-9}, {1e-12, 1e-10}, {1e-14, 1e-13}}
                              : std::vector<std::pair<double, double>>{{1e-11, 1e-9}, {1e-13, 1e-11}};
        bool qualified = true;
        unsigned accepted = 0, rejected = 0;
        nativels::Result result;
        auto start = std::chrono::steady_clock::now();
        for (auto [pt, qt] : tolerances)
        {
            result = nativels::hybridMinimize(w, p->cpu, state, p->dy, p->set, count, nativels::Mode::NativeExtended,
                                              pt, qt);
            qualified &= result.diagnostics.qualified;
            accepted += result.accepted;
            rejected += result.rejected;
            if (!qualified)
                break;
        }
        double wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        fs::create_directories(out / name);
        jointcpu::writeState(out / name / "final.json", state);
        std::ostringstream o;
        o << std::setprecision(17) << "{\"qualified\":" << (qualified ? "true" : "false")
          << ",\"accepted\":" << accepted << ",\"rejected\":" << rejected << ",\"F\":" << result.diagnostics.objective
          << ",\"positionResidual\":" << result.diagnostics.positionCorrection
          << ",\"qResidual\":" << result.diagnostics.stationarity << ",\"spectral\":" << result.diagnostics.spectral
          << ",\"hybridWallMs\":" << wall << '}';
        text(out / name / "result.json", o.str());
        cases++;
        passed += qualified;
        std::cout << name << " qualified=" << qualified << " accepted=" << accepted << '\n';
    }
    std::ostringstream log;
    log << std::setprecision(17) << '[';
    bool first = true;
    for (auto r : hybridRecords)
    {
        if (!first)
            log << ',';
        first = false;
        log << "{\"n\":" << r.n << ",\"corrections\":" << r.corrections << ",\"gpuMs\":" << r.gpu
            << ",\"uploadWaitMs\":" << r.upload << ",\"solveReadbackWaitMs\":" << r.roundtrip << ",\"code\":" << r.code
            << '}';
    }
    log << ']';
    text(out / "linear-calls.json", log.str());
    text(out / "STATUS.json", "{\"cases\":" + std::to_string(cases) + ",\"passed\":" + std::to_string(passed) + "}");
    c.audit();
    return cases == 12 && passed == 12 ? 0 : 1;
}
} // namespace gpuls
