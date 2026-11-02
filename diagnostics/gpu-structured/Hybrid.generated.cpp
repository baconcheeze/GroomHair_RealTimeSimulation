#include "Hybrid.h"
namespace gpuls
{
Engine *hybridEngine = nullptr;
std::vector<HybridRecord> hybridRecords;
} // namespace gpuls
namespace nativels
{
#include "../native-ls/LocalEquations.generated.h"
bool gpuLinear(Workspace &w, unsigned count)
{
    require(gpuls::hybridEngine != nullptr, "No diagnostic GPU backend");
    gpuls::Pack pack(1);
    pack.system(0, w);
    auto &engine = *gpuls::hybridEngine;
    engine.prepare(pack);
    auto r = engine.run(count);
    gpuls::Status status;
    std::memcpy(&status, r.bytes.data() + pack.systems[0].statusOffset, sizeof(status));
    gpuls::hybridRecords.push_back({r.total, engine.uploadWall, r.wall, w.n, count, status.code});
    if (status.code || status.done != 1 || status.corrections != count)
        return false;
    w.timing.factors++;
    w.timing.solves += count + 1;
    w.timing.refinements += count;
    std::memcpy(w.u.data(), r.bytes.data() + pack.systems[0].outputOffset, w.n * 8);
    for (unsigned j = 0; j < w.n; j++)
        w.delta[j] = w.u[j] / w.scale[j];
    return true;
}
Result hybridMinimize(Workspace &w, const GuideCpuState &c, windowmath::State &s, const std::vector<D> &y,
                      const windowmath::Settings &settings, unsigned count, Mode mode, double ptol, double qtol)
{
    auto begin = Clock::now();
    for (unsigned j = 0; j < w.layout.q.size(); j++)
        if (w.layout.q[j] >= 0)
            s.q[j] = s.q[j].unit();
    double lambda = 1e-8;
    Result result;
    auto &trial = w.trial;
    for (unsigned iteration = 0; iteration < 1500; iteration++)
    {
        w.assemble(c, s, y, settings);
        result.diagnostics = diagnoseBands(c, s, settings, w.layout, w);
        result.diagnostics.iterations = iteration;
        require(std::isfinite(result.diagnostics.objective), "Nonfinite objective");
        if (result.diagnostics.positionCorrection < ptol && result.diagnostics.stationarity < qtol &&
            result.diagnostics.spectral < qtol)
        {
            result.diagnostics.qualified = true;
            w.timing.nonlinear += ms(begin);
            return result;
        }
        bool accepted = false;
        for (unsigned retry = 0; retry < 16; retry++)
        {
            w.prepare(lambda);
            if (!gpuLinear(w, count))
            {
                result.factorFailure = true;
                w.timing.nonlinear += ms(begin);
                return result;
            }
            for (unsigned line = 0; line < 16; line++)
            {
                auto t = Clock::now();
                applyInto(trial, s, w.layout, w.delta, std::ldexp(1., -int(line)));
                double f = objective(c, trial, y, settings, w.layout);
                w.timing.trial += ms(t);
                w.timing.trials++;
                if (std::isfinite(f) && f < w.energy)
                {
                    s = trial;
                    accepted = true;
                    result.accepted++;
                    break;
                }
                result.rejected++;
            }
            if (accepted)
            {
                lambda = std::max(1e-12, lambda * .2);
                break;
            }
            lambda = std::min(1e12, lambda * 10);
        }
        if (!accepted)
        {
            result.noAcceptedTrial = true;
            break;
        }
    }
    w.timing.nonlinear += ms(begin);
    return result;
}

} // namespace nativels
