#include "Dynamics.h"
#include "OrientationFixtures.h"
#include <fstream>
#include <iomanip>
#include <sstream>
using hair::D;
using hair::F;
using hair::require;
namespace
{
void write(const std::filesystem::path &path, const std::string &text)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    f << text;
    f.flush();
    require(bool(f), "Dynamics report failure");
}
struct Fixture
{
    Strands strands;
    GuideCpuState cpu;
    guidegpu::State gpu;
    WindowSolver window;
    DynamicsSolver dynamics;
    Fixture(const OrientationTestContext &ctx, unsigned vertices = 5)
    {
        strands.offsets = {0, vertices, vertices + 3};
        strands.sourceIds = {0, 1};
        for (unsigned j = 0; j < vertices; j++)
            strands.positions.push_back({0, 0, .01f * (float(j) - float(vertices - 1) * .5f)});
        for (unsigned j = 0; j < 3; j++)
            strands.positions.push_back({.03f, 0, .01f * j});
        strands.derive();
        cpu.initialize(strands);
        UploadList keep;
        ctx.submit([&](auto *list) {
            gpu.initialize(ctx.device, list, cpu, nullptr, nullptr, keep);
            window.initialize(ctx.device, list, gpu, ctx.shaders.parent_path() / "window", keep);
            dynamics.initialize(ctx.device, list, gpu, window, ctx.shaders.parent_path() / "dynamics", keep);
        });
    }
    void reset(const OrientationTestContext &ctx)
    {
        UploadList keep;
        ctx.submit([&](auto *list) { dynamics.reset(ctx.device, list, keep); });
    }
};
double xError(const windowmath::State &a, const windowmath::State &b)
{
    double e = 0;
    for (size_t i = 0; i < a.x.size(); i++)
        e = std::max(e, (a.x[i] - b.x[i]).norm());
    return e;
}
double qError(const windowmath::State &a, const windowmath::State &b)
{
    double e = 0;
    for (size_t i = 0; i < a.q.size(); i++)
        e = std::max(e, hair::angle(a.q[i], b.q[i]));
    return e;
}
void uploadVelocity(guidegpu::State &gpu, const std::vector<Float3> &v, const OrientationTestContext &ctx)
{
    UploadList keep;
    ctx.submit([&](auto *list) {
        gpu.velocity.upload(ctx.device, list, v.data(), v.size() * 12, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    });
}
DynamicsSnapshot advance(DynamicsSolver &dynamics, const OrientationTestContext &ctx, unsigned n)
{
    ctx.submit([&](auto *list) { dynamics.step(list, n); });
    auto s = dynamics.read(ctx.device, ctx.submit);
    dynamics.diagnostics(ctx.device, ctx.submit, s);
    return s;
}
} // namespace
void testDynamicsLocal(const OrientationTestContext &ctx)
{
    for (unsigned vertices : {2u, 3u, 5u})
        for (unsigned pins = 0; pins < 4; pins++)
        {
            Fixture boundary(ctx, vertices);
            boundary.dynamics.settings.pinX = (pins & 1) != 0;
            boundary.dynamics.settings.pinQ = (pins & 2) != 0;
            boundary.reset(ctx);
            advance(boundary.dynamics, ctx, 4);
        }
    double translationX = 0, translationV = 0, fallX = 0, fallV = 0, driftX = 0, driftQ = 0, driftV = 0;
    for (unsigned kind = 0; kind < 3; kind++)
    {
        Fixture f(ctx);
        f.dynamics.settings.pinX = kind == 0;
        f.dynamics.settings.pinQ = kind == 0;
        f.dynamics.gravity = kind == 2 ? Float3{0, -9.81f, 0} : Float3{};
        f.reset(ctx);
        auto v = f.cpu.velocities;
        if (kind)
            for (unsigned j = 0; j < 5; j++)
                v[j] = {.015f, .02f, -.01f};
        uploadVelocity(f.gpu, v, ctx);
        auto before = windowmath::rest(f.cpu);
        auto s = advance(f.dynamics, ctx, 16);
        double h = f.dynamics.settings.h;
        for (unsigned j = 0; j < 5; j++)
        {
            D initial{v[j].x, v[j].y, v[j].z}, g{f.dynamics.gravity.x, f.dynamics.gravity.y, f.dynamics.gravity.z};
            D expected = before.x[j] + (16 * h) * initial + (136 * h * h) * g, velocity = initial + (16 * h) * g,
              actualV{s.velocity[j].x, s.velocity[j].y, s.velocity[j].z};
            double xe = (s.guide.state.x[j] - expected).norm(), ve = (actualV - velocity).norm();
            if (kind == 0)
            {
                driftX = std::max(driftX, xe);
                driftV = std::max(driftV, ve);
            }
            else if (kind == 1)
            {
                translationX = std::max(translationX, xe);
                translationV = std::max(translationV, ve);
            }
            else
            {
                fallX = std::max(fallX, xe);
                fallV = std::max(fallV, ve);
            }
        }
        if (kind == 0)
            driftQ = qError(before, s.guide.state);
    }
    require(driftX < 2e-6 && driftQ < 2e-4 && driftV < 2e-4, "Zero gravity rest drift");
    require(translationX < 5e-6 && translationV < 2e-4 && fallX < 5e-6 && fallV < 2e-4,
            "Discrete translation/free-fall reference");
    // Independently prepare CPU-predicted x/y and use the old fixed-target path.
    Fixture dynamic(ctx), reference(ctx);
    dynamic.reset(ctx);
    reference.reset(ctx);
    auto v = dynamic.cpu.velocities;
    for (unsigned j = 1; j < 5; j++)
        v[j] = {.002f * j, .003f * j, -.001f * j};
    uploadVelocity(dynamic.gpu, v, ctx);
    auto start = windowmath::rest(dynamic.cpu);
    auto q = dynamic.cpu.perturb("smooth", 1, true);
    UploadList uploads;
    ctx.submit([&](auto *list) { dynamic.gpu.reset(ctx.device, list, q, uploads); });
    std::vector<Float3> target = dynamic.cpu.source->positions;
    float h = dynamic.dynamics.settings.h;
    auto g = dynamic.dynamics.gravity;
    for (unsigned j = 1; j < 5; j++)
    {
        auto &y = target[j];
        y.x = y.x + h * v[j].x + (h * h) * g.x;
        y.y = y.y + h * v[j].y + (h * h) * g.y;
        y.z = y.z + h * v[j].z + (h * h) * g.z;
    }
    ctx.submit([&](auto *list) { dynamic.dynamics.begin(list); });
    auto predicted = dynamic.dynamics.read(ctx.device, ctx.submit);
    double predictionError = 0;
    for (unsigned j = 0; j < 5; j++)
        predictionError =
            std::max(predictionError, (predicted.guide.state.x[j] - D{target[j].x, target[j].y, target[j].z}).norm());
    auto beforeEnergy =
        windowmath::measure(dynamic.cpu, predicted.guide.state, predicted.target, dynamic.dynamics.settings).total();
    ctx.submit([&](auto *list) { dynamic.dynamics.solve(list, true); });
    auto solved = dynamic.dynamics.read(ctx.device, ctx.submit);
    require(std::memcmp(predicted.target.data(), solved.target.data(), target.size() * 12) == 0 &&
                std::memcmp(predicted.previous.data(), solved.previous.data(), target.size() * 12) == 0,
            "Target/history changed during sweeps");
    ctx.submit([&](auto *list) { dynamic.dynamics.end(list); });
    auto final = dynamic.dynamics.read(ctx.device, ctx.submit);
    auto diagnostic = dynamic.dynamics.diagnostics(ctx.device, ctx.submit, final);
    reference.window.settings = dynamic.dynamics.settings;
    ctx.submit([&](auto *list) {
        reference.gpu.position.upload(ctx.device, list, target.data(), target.size() * 12, uploads,
                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        reference.gpu.reset(ctx.device, list, q, uploads);
        reference.window.prepare(ctx.device, list, target, "equivalence", uploads);
        reference.window.run(list, dynamic.dynamics.sweeps);
    });
    auto ref = reference.window.read(ctx.device, ctx.submit);
    double equivalenceX = xError(final.guide.state, ref.state), equivalenceQ = qError(final.guide.state, ref.state);
    auto afterEnergy =
        windowmath::measure(dynamic.cpu, final.guide.state, final.target, dynamic.dynamics.settings).total();
    require(predictionError < 1e-8 && equivalenceX < 2e-7 && equivalenceQ < 3e-5 && afterEnergy <= beforeEnergy + 1e-11,
            "One substep equivalence/objective");
    // Step grouping and exact reset/replay use the same fixed h and sweep budget.
    Fixture groups(ctx);
    groups.reset(ctx);
    auto batch = advance(groups.dynamics, ctx, 8);
    groups.reset(ctx);
    DynamicsSnapshot singles;
    for (unsigned i = 0; i < 8; i++)
        singles = advance(groups.dynamics, ctx, 1);
    require(xError(batch.guide.state, singles.guide.state) == 0 &&
                qError(batch.guide.state, singles.guide.state) < 1e-12,
            "Step N vs Step One");
    groups.reset(ctx);
    ctx.submit([&](auto *list) { groups.dynamics.step(list, 3); });
    ctx.submit([&](auto *list) { groups.dynamics.step(list, 5); });
    auto distributed = groups.dynamics.read(ctx.device, ctx.submit);
    require(xError(batch.guide.state, distributed.guide.state) == 0, "Grouped frame submissions diverged");
    double priorTime=groups.dynamics.simulationTime();
    groups.dynamics.settings.h *= 2;
    require(groups.dynamics.simulationTime()==priorTime,"Editing stale h changed elapsed simulated time");
    bool stale = false;
    try
    {
        groups.dynamics.step(nullptr, 1);
    }
    catch (const std::exception &)
    {
        stale = true;
    }
    require(stale, "Stale dynamics accepted");
    groups.dynamics.settings.h *= .5f;
    require(!groups.dynamics.ready(), "Stale configuration resurrected");
    // Sticky prediction failure: no end velocity update and no later step resumes the strand.
    groups.reset(ctx);
    auto bad = groups.cpu.velocities;
    bad[1].x = NAN;
    uploadVelocity(groups.gpu, bad, ctx);
    ctx.submit([&](auto *list) {
        groups.dynamics.step(list, 4);
        groups.dynamics.recordStatusPoll(list, 0);
    });
    groups.dynamics.observe(0);
    auto failure = groups.dynamics.read(ctx.device, ctx.submit);
    require(groups.dynamics.failed && failure.status[0].code == 8 && failure.status[0].substep == 1 &&
                failure.status[0].completed == 0 && failure.status[0].failures == 1,
            "Failure not sticky across substeps");
    groups.reset(ctx);
    groups.gpu.verifyImmutable(ctx.device, ctx.submit, true);
    {
        Fixture unresolved(ctx, 3);
        unresolved.reset(ctx);
        auto invalidQ = unresolved.cpu.orientations;
        invalidQ[1] = {};
        UploadList keep;
        ctx.submit([&](auto *list) {
            unresolved.gpu.reset(ctx.device, list, invalidQ, keep);
            unresolved.dynamics.step(list, 3);
        });
        auto failed = unresolved.dynamics.read(ctx.device, ctx.submit);
        require(failed.status[0].code == 2 && failed.status[0].substep == 1 && failed.status[0].completed == 0,
                "Unresolved window failure was completed");
        require(std::memcmp(failed.velocity.data(), unresolved.cpu.velocities.data(), failed.velocity.size() * 12) == 0,
                "Velocity reconstructed after failed solve");
    }
    FixedStepClock clock;
    double ch = 1. / 120;
    unsigned ticks = 0;
    for (unsigned i = 0; i < 60; i++)
        ticks += clock.advance(1. / 60, ch, true);
    require(ticks == 120, "Fixed clock frequency");
    clock.advance(10, ch, false);
    require(clock.remainder == 0, "Pause retained catchup");
    require(clock.advance(1., ch, true) == 8 && clock.discarded > .9, "Stall catchup cap");
    std::ostringstream out;
    out.precision(17);
    out << "{\"test\":\"dynamics-local\",\"zeroGravityPositionDrift\":" << driftX
        << ",\"zeroGravityQuaternionDrift\":" << driftQ << ",\"zeroGravityVelocityDrift\":" << driftV
        << ",\"translationPositionError\":" << translationX << ",\"translationVelocityError\":" << translationV
        << ",\"freeFallPositionError\":" << fallX << ",\"freeFallVelocityError\":" << fallV
        << ",\"predictionError\":" << predictionError << ",\"equivalencePositionError\":" << equivalenceX
        << ",\"equivalenceQuaternionError\":" << equivalenceQ << ",\"objectiveBefore\":" << beforeEnergy
        << ",\"objectiveAfter\":" << afterEnergy
        << ",\"groupingResetReplayExact\":true,\"stickyFailurePassed\":true,\"clockPassed\":true,\"boundaryPinCases\":"
           "12,\"failedSolveSkipsVelocity\":true,"
           "\"equivalenceDiagnostics\":"
        << diagnostic << "}";
    write(ctx.report, out.str());
}
void testDynamicsGroom(guidegpu::State &state, WindowSolver &window, DynamicsSolver &dynamics,
                       const OrientationTestContext &ctx, ID3D12CommandQueue *queue,Float3 gravitySolver)
{
    std::ostringstream out, csv;
    out.precision(15);
    csv << "strands,h,sweeps,steps,tip,speed,kinetic,axialStrain,positionCorrection,qResidual\n";
    out << "{\"test\":\"dynamics-groom\",\"cases\":[";
    bool first = true;
    std::vector<windowmath::State> budgetStates;
    auto run = [&](unsigned selection, float h, unsigned sweeps, unsigned steps, bool velocity, bool noGravity) {
        dynamics.settings = {selection, h, 1, 1e-5f, true, true};
        dynamics.diagnosticSnapshots = true;
        dynamics.sweeps = sweeps;
        dynamics.gravity = noGravity ? Float3{} : gravitySolver;
        UploadList keep;
        ctx.submit([&](auto *list) {
            dynamics.reset(ctx.device, list, keep);
            if (velocity)
                dynamics.kick(list, {.01f, .02f, .005f});
            dynamics.step(list, steps);
        });
        auto snapshot = dynamics.read(ctx.device, ctx.submit);
        std::string report = dynamics.diagnostics(ctx.device, ctx.submit, snapshot);
        auto e = windowmath::measure(dynamics.cpu(), snapshot.guide.state, snapshot.target, dynamics.settings);
        double speed = 0, kinetic = 0, tip = 0, strain = 0;
        unsigned count = selection ? selection : unsigned(dynamics.cpu().ranges.size());
        for (unsigned i = 0; i < count; i++)
        {
            auto r = dynamics.cpu().ranges[i];
            unsigned t = r.firstVertex + r.vertexCount - 1;
            auto p = dynamics.cpu().source->positions[t];
            tip = std::max(tip, (snapshot.guide.state.x[t] - D{p.x, p.y, p.z}).norm());
            for (unsigned j = 0; j < r.vertexCount; j++)
            {
                auto v = snapshot.velocity[r.firstVertex + j];
                double n = D{v.x, v.y, v.z}.norm();
                speed = std::max(speed, n);
                kinetic += .5 * dynamics.cpu().masses[r.firstVertex + j] * n * n;
            }
            for (unsigned j = 0; j < r.segmentCount; j++)
                strain = std::max(strain, std::abs((snapshot.guide.state.x[r.firstVertex + j + 1] -
                                                    snapshot.guide.state.x[r.firstVertex + j])
                                                           .norm() /
                                                       dynamics.cpu().lengths[r.firstSegment + j] -
                                                   1));
        }
        if (!first)
            out << ',';
        first = false;
        out << "{\"strands\":" << count << ",\"velocityKick\":" << (velocity ? "true" : "false")
            << ",\"diagnostics\":" << report << "}";
        csv << std::setprecision(15) << count << ',' << h << ',' << sweeps << ',' << steps << ',' << tip << ',' << speed
            << ',' << kinetic << ',' << strain << ',' << e.correction << ',' << e.stationarity << '\n';
        return snapshot;
    };
    for (unsigned selection : {1u, 10u, 100u, 0u})
        run(selection, 1.f / 120, 8, 16, false, false);
    for (unsigned sweeps : {1u, 4u, 8u, 16u, 32u})
        budgetStates.push_back(run(10, 1.f / 120, sweeps, 16, false, false).guide.state);
    auto finer = run(10, 1.f / 240, 32, 32, false, false);
    auto velocity = run(10, 1.f / 120, 8, 16, true, true);
    auto firstTip = dynamics.cpu().source->positions[31];
    require((velocity.guide.state.x[31] - D{firstTip.x, firstTip.y, firstTip.z}).norm() > 1e-6,
            "Velocity response absent");
    run(10, 1.f / 120, 8, 16, false, true);
    // Bounded warm-up and timestamp-only samples. Explicit diagnostic CPU readbacks excluded.
    std::string timing = "null";
    if (queue && !ctx.software)
    {
        dynamics.settings = {10, 1.f / 120, 1, 1e-5f, true, true};
        dynamics.gravity = gravitySolver;
        dynamics.diagnosticSnapshots = false;
        dynamics.sweeps = 8;
        UploadList keep;
        ctx.submit([&](auto *list) {
            dynamics.reset(ctx.device, list, keep);
            dynamics.step(list, 8);
        });
        DynamicsTiming timer;
        timer.initialize(ctx.device, queue);
        std::array<std::vector<double>, 3> samples;
        for (unsigned i = 0; i < 24; i++)
        {
            ctx.submit([&](auto *list) { dynamics.step(list, 1, &timer); });
            auto ms = timer.milliseconds();
            for (unsigned p = 0; p < 3; p++)
                samples[p].push_back(ms[p]);
        }
        dynamics.diagnostics(ctx.device, ctx.submit, dynamics.read(ctx.device, ctx.submit));
        std::ostringstream t;
        t.precision(12);
        t << "{\"queueFrequency\":" << timer.frequency
          << ",\"warmup\":8,\"samples\":24,\"strands\":10,\"sweeps\":8,\"phaseMedianMs\":[";
        for (unsigned p = 0; p < 3; p++)
        {
            std::sort(samples[p].begin(), samples[p].end());
            if (p)
                t << ',';
            t << .5 * (samples[p][11] + samples[p][12]);
        }
        t << "],\"phaseP95Ms\":[";
        for (unsigned p = 0; p < 3; p++)
        {
            if (p)
                t << ',';
            t << samples[p][22];
        }
        t << "],\"diagnosticReadbacksIncluded\":false}";
        timing = t.str();
    }
    out << "],\"budgetPositionDifferencesVs32\":[";
    for (unsigned i = 0; i < budgetStates.size(); i++)
    {
        if (i)
            out << ',';
        out << xError(budgetStates[i], budgetStates.back());
    }
    out << "],\"differentHPositionDifference\":" << xError(finer.guide.state, budgetStates.back())
        << ",\"gpuTiming\":" << timing << "}";
    auto csvPath = ctx.report;
    csvPath.replace_extension("csv");
    write(csvPath, csv.str());
    write(ctx.report, out.str());
    UploadList keep;
    ctx.submit([&](auto *list) {
        dynamics.reset(ctx.device, list, keep);
        window.resetCurrent(ctx.device, list, keep);
    });
    state.verifyImmutable(ctx.device, ctx.submit, true);
}
