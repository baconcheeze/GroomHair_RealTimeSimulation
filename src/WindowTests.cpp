#include "WindowTests.h"
#include "OrientationFixtures.h"
#include <fstream>
#include <iomanip>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace
{
using hair::D;
using hair::F;
using hair::require;
void write(const std::filesystem::path &path, const std::string &value)
{
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream f(path);
    require(bool(f), "Cannot open window report");
    f << value << '\n';
    f.flush();
    require(bool(f), "Window report write failure");
}
void upload(guidegpu::State &gpu, const windowmath::State &state, const OrientationTestContext &ctx)
{
    std::vector<Float3> x;
    std::vector<F> q;
    for (auto p : state.x)
        x.push_back({float(p.x), float(p.y), float(p.z)});
    for (auto p : state.q)
        q.emplace_back(p);
    UploadList keep;
    ctx.submit([&](auto *list) {
        gpu.position.upload(ctx.device, list, x.data(), x.size() * 12, keep,
                            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        gpu.reset(ctx.device, list, q, keep);
    });
}
void prepare(WindowSolver &solver, const OrientationTestContext &ctx, const std::vector<Float3> &y)
{
    UploadList keep;
    ctx.submit([&](auto *list) { solver.prepare(ctx.device, list, y, "test", keep); });
}
WindowSnapshot run(WindowSolver &solver, const OrientationTestContext &ctx, unsigned sweeps, unsigned flags = 0,
                   unsigned limit = 0, unsigned chunk = 8)
{
    ctx.submit([&](auto *list) { solver.run(list, sweeps, flags, limit, chunk); });
    auto result = solver.read(ctx.device, ctx.submit);
    for (auto f : result.faults)
        require(f.code == 0, "Fixture failure code=" + std::to_string(f.code) + " strand=" + std::to_string(f.strand) +
                                 " op=" + std::to_string(f.operation) + " index=" + std::to_string(f.index));
    return result;
}
struct Fixture
{
    Strands strands;
    GuideCpuState cpu;
    guidegpu::State gpu;
    WindowSolver solver;
    Fixture(const OrientationTestContext &ctx, const Strands &source) : strands(source)
    {
        cpu.initialize(strands);
        UploadList keep;
        ctx.submit([&](auto *list) {
            gpu.initialize(ctx.device, list, cpu, nullptr, nullptr, keep);
            solver.initialize(ctx.device, list, gpu, ctx.shaders.parent_path() / "window", keep);
        });
    }
    Fixture(const OrientationTestContext &ctx, std::vector<unsigned> lengths)
    {
        strands.offsets.push_back(0);
        for (unsigned n : lengths)
        {
            unsigned id = unsigned(strands.sourceIds.size());
            strands.sourceIds.push_back(id);
            for (unsigned j = 0; j < n; ++j)
            {
                float t = float(j);
                strands.positions.push_back(
                    {.03f * id + .0001f * t * t, .0005f * std::sin(t), .006f * t + .0001f * t * t});
            }
            strands.offsets.push_back(unsigned(strands.positions.size()));
        }
        strands.derive();
        cpu.initialize(strands);
        UploadList keep;
        ctx.submit([&](auto *list) {
            gpu.initialize(ctx.device, list, cpu, nullptr, nullptr, keep);
            solver.initialize(ctx.device, list, gpu, ctx.shaders.parent_path() / "window", keep);
        });
    }
};
windowmath::State perturbed(const GuideCpuState &cpu, const windowmath::Settings &settings)
{
    auto state = windowmath::rest(cpu);
    unsigned n = settings.participating ? std::min(settings.participating, unsigned(cpu.ranges.size()))
                                        : unsigned(cpu.ranges.size());
    for (unsigned k = 0; k < n; ++k)
    {
        auto r = cpu.ranges[k];
        for (unsigned j = 0; j < r.vertexCount; ++j)
        {
            if (j == 0 && settings.pinX)
                continue;
            double t = (j + .3) / r.vertexCount;
            auto &x = state.x[r.firstVertex + j];
            x = x + D{.001 * std::sin(2 * t), -.0008 * t, .0007 * std::cos(t)};
        }
        for (unsigned j = 0; j < r.segmentCount; ++j)
        {
            if (j == 0 && settings.pinQ)
                continue;
            auto &q = state.q[r.firstSegment + j];
            q = (q * hair::axisAngle(D{1, .3, .2}, .12 + .1 * j / r.segmentCount)).unit();
        }
    }
    return state;
}
double positionDifference(const windowmath::State &a, const windowmath::State &b)
{
    double error = 0;
    for (size_t i = 0; i < a.x.size(); ++i)
        error = std::max(error, (a.x[i] - b.x[i]).norm());
    return error;
}
double quaternionDifference(const windowmath::State &a, const windowmath::State &b)
{
    double error = 0;
    for (size_t i = 0; i < a.q.size(); ++i)
        error = std::max(error, hair::angle(a.q[i], b.q[i]));
    return error;
}
std::vector<F> localGpu(const OrientationTestContext &ctx, const std::vector<F> &input)
{
    guidegpu::Buffer src, dst;
    UploadList keep;
    src.create(ctx.device, input.size() * 16, false, L"PositionOracleInputs");
    dst.create(ctx.device, input.size() / 6 * 32, true, L"PositionOracleOutputs");
    D3D12_ROOT_PARAMETER p[3]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants.Num32BitValues = 4;
    p[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    p[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 3;
    rs.pParameters = p;
    ComPtr<ID3DBlob> b, e;
    guidegpu::check(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e));
    ComPtr<ID3D12RootSignature> root;
    guidegpu::check(ctx.device->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&root)));
    auto path = ctx.shaders.parent_path() / "window/PositionValidation.dxil";
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    require(bool(f), "Missing position validation shader");
    auto size = f.tellg();
    std::vector<char> code(size_t(size), 0);
    f.seekg(0);
    f.read(code.data(), size);
    D3D12_COMPUTE_PIPELINE_STATE_DESC ps{};
    ps.pRootSignature = root.Get();
    ps.CS = {code.data(), code.size()};
    ComPtr<ID3D12PipelineState> pipeline;
    guidegpu::check(ctx.device->CreateComputePipelineState(&ps, IID_PPV_ARGS(&pipeline)));
    ctx.submit([&](auto *list) {
        src.upload(ctx.device, list, input.data(), input.size() * 16, keep,
                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        dst.transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetComputeRootSignature(root.Get());
        list->SetPipelineState(pipeline.Get());
        uint32_t c[4]{unsigned(input.size() / 6), 0, 0, 0};
        list->SetComputeRoot32BitConstants(0, 4, c, 0);
        list->SetComputeRootShaderResourceView(1, src.address());
        list->SetComputeRootUnorderedAccessView(2, dst.address());
        list->Dispatch((c[0] + 63) / 64, 1, 1);
        guidegpu::uavBarrier(list, dst.resource.Get());
    });
    return guidegpu::read<F>(dst, ctx.device, ctx.submit, input.size() / 6 * 2);
}
} // namespace
void testPositionLocal(const OrientationTestContext &ctx)
{
    auto cpu = windowmath::validatePositionMath();
    hair::CsRandom random(91031);
    std::vector<windowmath::PositionProblem> problems;
    std::vector<F> inputs;
    for (unsigned i = 0; i < 256; ++i)
    {
        auto v = [&]() {
            return F{float(random.sample() - .5), float(random.sample() - .5), float(random.sample() - .5)};
        };
        F x = v(), y = v(), l = v(), r = v(), tl = v(), tr = v();
        float a = .1f + float(random.sample()) * 5, cl = i % 3 == 0 ? 0 : .2f + float(random.sample()) * 12,
              cr = i % 3 == 1 ? 0 : .3f + float(random.sample()) * 9;
        bool pin = i % 11 == 0;
        inputs.insert(inputs.end(), {F{x.x, x.y, x.z, a}, F{y.x, y.y, y.z, cl}, F{l.x, l.y, l.z, cr},
                                     F{r.x, r.y, r.z, pin ? 1.f : 0.f}, tl, tr});
        problems.push_back({D(x), D(y), D(l), D(r), D(tl), D(tr), a, cl, cr, pin});
    }
    auto output = localGpu(ctx, inputs);
    double maxError = 0, maxGrowth = 0;
    for (size_t i = 0; i < problems.size(); ++i)
    {
        require(output[i * 2 + 1].x == 1, "GPU position local rejected");
        D actual{output[i * 2].x, output[i * 2].y, output[i * 2].z};
        auto expected = windowmath::positionMinimum(problems[i]);
        maxError = std::max(maxError, (actual - expected).norm());
        maxGrowth = std::max(maxGrowth, windowmath::localEnergy(problems[i], actual) -
                                            windowmath::localEnergy(problems[i], problems[i].x));
    }
    require(maxError < 5e-7 && maxGrowth < 1e-12, "GPU position local oracle");
    auto bad = inputs;
    bad[0].w = 0;
    bad[6].w = INFINITY;
    bad[12].x = NAN;
    auto rejected = localGpu(ctx, bad);
    require(rejected[1].x == 0 && rejected[3].x == 0 && rejected[5].x == 0, "Invalid position input accepted");
    // Actual groom-scale conditioning, not rescaled mass/stiffness.
    RestData actual;
    actual.load(ctx.shaders.parent_path().parent_path() / "assets");
    Strands strand;
    unsigned vertices = actual.guide.offsets[1] - actual.guide.offsets[0];
    strand.positions.assign(actual.guide.positions.begin(), actual.guide.positions.begin() + vertices);
    strand.offsets = {0, vertices};
    strand.sourceIds = {actual.guide.sourceIds[0]};
    strand.derive();
    Fixture f(ctx, strand);
    auto &set = f.solver.settings;
    set.pinX = true;
    set.pinQ = true;
    auto y = windowmath::targets(f.cpu, "displaced");
    auto original = windowmath::rest(f.cpu);
    std::vector<D> yd, t;
    std::vector<double> a, c;
    for (unsigned j = 0; j < vertices; ++j)
    {
        yd.emplace_back(y[j].x, y[j].y, y[j].z);
        a.push_back(f.cpu.masses[j] / (double(set.h) * set.h));
        if (j + 1 < vertices)
        {
            c.push_back(set.ss / f.cpu.lengths[j]);
            t.push_back(double(f.cpu.lengths[j]) * hair::rotate(original.q[j], D{0, 0, 1}));
        }
    }
    auto oracle = windowmath::tridiagonal(yd, a, c, t, true, original.x[0]);
    auto iter = original;
    for (unsigned n = 0; n < 16000; ++n)
        windowmath::uncachedSweep(f.cpu, iter, y, set, 0, true);
    double cpuTri = 0;
    for (unsigned v = 0; v < vertices; ++v)
        cpuTri = std::max(cpuTri, (iter.x[v] - oracle[v]).norm());
    require(cpuTri < 1e-9, "Actual-scale double iterative/tridiagonal oracle");
    prepare(f.solver, ctx, y);
    auto gpu = run(f.solver, ctx, 8192, 2);
    double gpuTri = 0;
    for (unsigned v = 0; v < vertices; ++v)
        gpuTri = std::max(gpuTri, (gpu.state.x[v] - oracle[v]).norm());
    std::vector<D> unitRhs(vertices), zeroEdges(vertices - 1);
    for (unsigned j = 0; j < vertices; ++j)
        unitRhs[j] = {1 / a[j], 0, 0};
    auto inverseRows = windowmath::tridiagonal(unitRhs, a, c, zeroEdges, true, {});
    double inverseBound = 0, maximumRow = 0;
    for (unsigned j = 1; j < vertices; ++j)
    {
        inverseBound = std::max(inverseBound, inverseRows[j].x);
        double row = a[j] + 2 * c[j - 1];
        if (j + 1 < vertices)
            row += 2 * c[j];
        maximumRow = std::max(maximumRow, row);
    }
    double conditionBound = inverseBound * maximumRow;
    auto energy = windowmath::measure(f.cpu, gpu.state, y, set);
    require(gpuTri < 5e-6 && energy.correction < 2e-8, "Actual-scale float fixed-q oracle / correction floor");
    std::ostringstream result;
    result.precision(17);
    result << "{\"test\":\"position-local\",\"CPU\":" << cpu << ",\"gpuCases\":256,\"maxGpuOracleError\":" << maxError
           << ",\"maxLocalObjectiveGrowth\":" << maxGrowth << ",\"actualScaleDoubleTridiagonalError\":" << cpuTri
           << ",\"actualScaleGpuTridiagonalError\":" << gpuTri << ",\"actualScaleFinal\":" << energy.json()
           << ",\"actualSourceStrandID\":" << strand.sourceIds[0] << ",\"actualVertices\":" << vertices
           << ",\"conditionInfinityBound\":" << conditionBound
           << ",\"gpuFixedQSweeps\":8192,\"invalidInputsRejected\":3}";
    write(ctx.report, result.str());
}
void testWindowSolver(const OrientationTestContext &ctx)
{
    double cacheX = 0, cacheQ = 0, cpuX = 0, cpuQ = 0, lastXError = 0, lastQError = 0;
    unsigned cases = 0;
    for (unsigned vertices : {2u, 3u, 4u, 5u, 32u})
        for (unsigned pins = 0; pins < 4; ++pins)
        {
            Fixture f(ctx, {vertices, 3, 5});
            bool unpreparedRejected = false;
            try
            {
                f.solver.run(nullptr, 1);
            }
            catch (const std::exception &)
            {
                unpreparedRejected = true;
            }
            require(unpreparedRejected, "Unprepared execution accepted");
            f.solver.settings.pinX = (pins & 1) != 0;
            f.solver.settings.pinQ = (pins & 2) != 0;
            f.solver.settings.participating = 2;
            auto initial = perturbed(f.cpu, f.solver.settings);
            auto inactive = f.cpu.ranges[2];
            initial.x[inactive.firstVertex + 1].x += .0007;
            initial.q[inactive.firstSegment + 1] =
                (initial.q[inactive.firstSegment + 1] * hair::axisAngle(D{0, 1, 0}, .3)).unit();
            upload(f.gpu, initial, ctx);
            initial = f.solver.read(ctx.device, ctx.submit).state;
            auto y = windowmath::targets(f.cpu, "displaced");
            for (unsigned prefix : {1u, 2u, 3u, 4u, 5u, 8u, 12u, 0u})
            {
                upload(f.gpu, initial, ctx);
                prepare(f.solver, ctx, y);
                auto cached = run(f.solver, ctx, 1, 0, prefix);
                upload(f.gpu, initial, ctx);
                prepare(f.solver, ctx, y);
                auto uncached = run(f.solver, ctx, 1, 1, prefix);
                double dx = positionDifference(cached.state, uncached.state),
                       dq = quaternionDifference(cached.state, uncached.state);
                cacheX = std::max(cacheX, dx);
                cacheQ = std::max(cacheQ, dq);
                require(dx < 2e-7 && dq < 3e-5, "Cached / immediate-array GPU prefix mismatch");
                auto cpu = initial;
                windowmath::uncachedSweep(f.cpu, cpu, y, f.solver.settings, prefix);
                cpuX = std::max(cpuX, positionDifference(cached.state, cpu));
                cpuQ = std::max(cpuQ, quaternionDifference(cached.state, cpu));
                ++cases;
            }
            upload(f.gpu, initial, ctx);
            prepare(f.solver, ctx, y);
            auto cached = run(f.solver, ctx, 17);
            auto diag = f.solver.diagnostics(ctx.device, ctx.submit, cached);
            (void)diag;
            for (unsigned k = 0; k < 2; ++k)
            {
                auto r = f.cpu.ranges[k];
                auto s = cached.stats[k];
                require(s.attemptedP == 17 * (2 * r.segmentCount + 1) && s.attemptedQ == 17 * (2 * r.segmentCount - 1),
                        "Scheduled attempted counts");
                require(s.executedP == 17 * (2 * r.segmentCount + 1 - (f.solver.settings.pinX ? 1 : 0)) &&
                            s.executedQ == 17 * (2 * r.segmentCount - 1 - (f.solver.settings.pinQ ? 1 : 0)),
                        "Scheduled free DOF visit counts");
            }
            upload(f.gpu, initial, ctx);
            prepare(f.solver, ctx, y);
            auto uncached = run(f.solver, ctx, 17, 1, 0, 1);
            require(positionDifference(cached.state, uncached.state) < 2e-7 &&
                        quaternionDifference(cached.state, uncached.state) < 3e-5,
                    "Cache or chunk boundary mismatch");
            cacheX = std::max(cacheX, positionDifference(cached.state, uncached.state));
            cacheQ = std::max(cacheQ, quaternionDifference(cached.state, uncached.state));
            upload(f.gpu, initial, ctx);
            prepare(f.solver, ctx, y);
            auto beforeLast = run(f.solver, ctx, 16);
            auto replay = run(f.solver, ctx, 1);
            require(positionDifference(cached.state, replay.state) < 2e-7, "No extra sweep replay");
            for (unsigned k = 0; k < 2; ++k)
            {
                auto r = f.cpu.ranges[k];
                double x = 0, q = 0, bx = 0, bq = 0;
                for (unsigned j = 0; j < r.vertexCount; ++j)
                {
                    unsigned v = r.firstVertex + j;
                    x = std::max(x, (replay.state.x[v] - beforeLast.state.x[v]).norm());
                    bx = std::max(bx, (cached.state.x[v] - initial.x[v]).norm());
                }
                for (unsigned j = 0; j < r.segmentCount; ++j)
                {
                    unsigned s = r.firstSegment + j;
                    q = std::max(q, hair::angle(replay.state.q[s], beforeLast.state.q[s]));
                    bq = std::max(bq, hair::angle(cached.state.q[s], initial.q[s]));
                }
                lastXError = std::max(lastXError, std::abs(x - cached.stats[k].lastX));
                lastQError =
                    std::max(lastQError, std::abs(q - 4 * std::asin(std::min(1., double(cached.stats[k].lastQ) * .5))));
                require(std::abs(bx - cached.stats[k].batchX) < 1e-8 &&
                            std::abs(bq - 4 * std::asin(std::min(1., double(cached.stats[k].batchQ) * .5))) < 2e-6,
                        "True batch metric");
            }
            auto originalSettings = f.solver.settings;
            for (unsigned changed = 0; changed < 6; ++changed)
            {
                f.solver.settings = originalSettings;
                prepare(f.solver, ctx, y);
                if (changed == 0)
                    f.solver.settings.h *= 2;
                else if (changed == 1)
                    f.solver.settings.ss *= 2;
                else if (changed == 2)
                    f.solver.settings.bt *= 2;
                else if (changed == 3)
                    f.solver.settings.pinX = !originalSettings.pinX;
                else if (changed == 4)
                    f.solver.settings.pinQ = !originalSettings.pinQ;
                else
                    f.solver.settings.participating = 1;
                bool stale = false;
                try
                {
                    f.solver.run(nullptr, 1);
                }
                catch (const std::exception &)
                {
                    stale = true;
                }
                require(stale, "Stale problem accepted");
                f.solver.settings = originalSettings;
                require(!f.solver.ready(), "Observed stale problem must require explicit reprepare");
            }
            if (!originalSettings.pinX)
                require((cached.state.x[f.cpu.ranges[0].firstVertex] - initial.x[f.cpu.ranges[0].firstVertex]).norm() >
                            1e-9,
                        "Unpinned root was permanently fixed");
        }
    require(cpuX < 3e-6 && cpuQ < 2e-4 && lastXError < 1e-8 && lastQError < 2e-6,
            "Independent schedule/true last-sweep tolerance");
    // Large rotations and a controlled collapsed current edge, with VALID rest geometry.
    Fixture boundary(ctx, {2, 3, 4});
    boundary.solver.settings.pinX = false;
    boundary.solver.settings.pinQ = false;
    boundary.solver.settings.participating = 0;
    auto b = windowmath::rest(boundary.cpu);
    b.x[1] = b.x[0];
    for (auto &q : b.q)
        q = (q * hair::axisAngle(D{1, .2, 0}, 3.141592653589793)).unit();
    upload(boundary.gpu, b, ctx);
    prepare(boundary.solver, ctx, windowmath::targets(boundary.cpu, "rest"));
    auto after = run(boundary.solver, ctx, 32);
    boundary.solver.diagnostics(ctx.device, ctx.submit, after);
    // Errors and failure location remain sticky across chunks and later calls.
    Fixture invalid(ctx, {3});
    prepare(invalid.solver, ctx, windowmath::targets(invalid.cpu, "rest"));
    auto x = invalid.cpu.source->positions;
    x[1].x = NAN;
    UploadList keep;
    ctx.submit([&](auto *list) {
        invalid.gpu.position.upload(ctx.device, list, x.data(), x.size() * 12, keep,
                                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        invalid.solver.run(list, 17);
    });
    auto failed = invalid.solver.read(ctx.device, ctx.submit);
    require(failed.faults[0].code == 3 && failed.stats[0].failures == 1, "Invalid position / sticky chunk failure");
    ctx.submit([&](auto *list) { invalid.solver.run(list, 1); });
    auto sticky = invalid.solver.read(ctx.device, ctx.submit);
    require(std::memcmp(&failed.faults[0], &sticky.faults[0], sizeof(WindowFault)) == 0,
            "Failure location changed after later Run");
    for (unsigned failure : {2u, 5u, 6u, 7u})
    {
        Fixture bad(ctx, {3});
        bad.solver.settings.bt = failure == 6 ? std::numeric_limits<float>::max() : 1e-5f;
        bad.solver.settings.ss = failure == 7 ? std::numeric_limits<float>::max() : 1;
        auto start = windowmath::rest(bad.cpu);
        if (failure == 2)
            start.q[1] = {};
        if (failure == 5)
            start.x[0].x += .001;
        upload(bad.gpu, start, ctx);
        prepare(bad.solver, ctx, windowmath::targets(bad.cpu, "rest"));
        ctx.submit([&](auto *list) { bad.solver.run(list, 17); });
        auto status = bad.solver.read(ctx.device, ctx.submit);
        require(status.faults[0].code == failure && status.stats[0].failures == 1,
                "Expected isolated sticky failure code " + std::to_string(failure));
    }
    {
        Fixture bad(ctx, {3});
        auto y = windowmath::targets(bad.cpu, "rest");
        UploadList invalidUploads;
        bad.solver.settings.h = 0;
        bool rejected = false;
        try
        {
            bad.solver.prepare(ctx.device, nullptr, y, "invalid", invalidUploads);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected, "Zero h accepted");
        bad.solver.settings.h = 1.f / 120;
        y[1].x = NAN;
        rejected = false;
        try
        {
            bad.solver.prepare(ctx.device, nullptr, y, "invalid", invalidUploads);
        }
        catch (const std::exception &)
        {
            rejected = true;
        }
        require(rejected, "Nonfinite target accepted");
    }
    std::ostringstream result;
    result.precision(17);
    result << "{\"test\":\"window-solver\",\"prefixCases\":" << cases << ",\"cachedUncachedPositionError\":" << cacheX
           << ",\"cachedUncachedQuaternionRadians\":" << cacheQ << ",\"doubleSchedulePositionError\":" << cpuX
           << ",\"doubleScheduleQuaternionRadians\":" << cpuQ << ",\"trueLastSweepPositionMetricError\":" << lastXError
           << ",\"trueLastSweepOrientationMetricError\":" << lastQError
           << ",\"pinsAndInactiveAndChunksPassed\":true,\"staleRejected\":true,\"largeRotationCollapsePassed\":true,"
              "\"stickyFailurePassed\":true}";
    write(ctx.report, result.str());
}
void testWindowGroom(guidegpu::State &state, WindowSolver &solver, const OrientationTestContext &ctx)
{
    std::ostringstream rows, csv;
    csv << std::setprecision(17)
        << "case,strands,sweeps,inertia,SS,BT,total,positionGradient,positionCorrection,quaternionStationarity,"
           "spectral\n";
    bool first = true;
    uint64_t attemptedP = 0, executedP = 0, attemptedQ = 0, executedQ = 0, ordinary = 0, fallback = 0, hard = 0,
             failures = 0;
    double maxCheckpointGrowth = 0, maxRestPositionDrift = 0, maxRestOrientationDrift = 0;
    for (unsigned selection : {1u, 10u, 100u, 0u})
        for (const std::string mode : {"rest", "position", "orientation", "joint", "target"})
        {
            UploadList keep;
            ctx.submit([&](auto *list) { solver.resetCurrent(ctx.device, list, keep); });
            solver.settings = {selection, 1.f / 120, 1, 1e-5f, true, true};
            auto target = windowmath::targets(solver.cpu(), mode == "target" ? "displaced" : "rest");
            prepare(solver, ctx, target);
            if (mode != "rest" && mode != "target")
                ctx.submit([&](auto *list) {
                    solver.perturb(list, mode == "position" ? 1 : mode == "orientation" ? 2 : 3);
                });
            auto initial = solver.read(ctx.device, ctx.submit);
            auto previous = windowmath::measure(solver.cpu(), initial.state, target, solver.settings);
            double initialEnergy = previous.total();
            unsigned done = 0;
            WindowSnapshot final;
            std::string finalDiagnostic;
            for (unsigned checkpoint : {1u, 2u, 4u, 8u, 10u, 16u, 32u, 128u})
            {
                final = run(solver, ctx, checkpoint - done);
                auto energy = windowmath::measure(solver.cpu(), final.state, target, solver.settings);
                maxCheckpointGrowth = std::max(maxCheckpointGrowth, energy.total() - previous.total());
                require(energy.total() <= previous.total() + std::max(1e-11, previous.total() * 1e-6),
                        "Fixed total objective increased: " + mode);
                if (mode == "rest")
                {
                    maxRestPositionDrift =
                        std::max(maxRestPositionDrift, positionDifference(initial.state, final.state));
                    maxRestOrientationDrift =
                        std::max(maxRestOrientationDrift, quaternionDifference(initial.state, final.state));
                    require(energy.total() < 1e-10, "Rest no-op objective");
                    require(positionDifference(initial.state, final.state) < 2e-6 &&
                                quaternionDifference(initial.state, final.state) < 2e-4,
                            "Rest no-op state tolerance");
                }
                finalDiagnostic = solver.diagnostics(ctx.device, ctx.submit, final);
                csv << mode << ',' << solver.participating() << ',' << checkpoint << ',' << energy.inertia << ','
                    << energy.ss << ',' << energy.bt << ',' << energy.total() << ',' << energy.gradient << ','
                    << energy.correction << ',' << energy.stationarity << ',' << energy.spectral << '\n';
                for (unsigned i = 0; i < solver.participating(); ++i)
                {
                    auto s = final.stats[i];
                    attemptedP += s.attemptedP;
                    executedP += s.executedP;
                    attemptedQ += s.attemptedQ;
                    executedQ += s.executedQ;
                    ordinary += s.ordinary;
                    fallback += s.fallback;
                    hard += s.hard;
                    failures += s.failures;
                }
                done = checkpoint;
                previous = energy;
            }
            if (mode != "rest")
                require(previous.total() < initialEnergy, "Coupled problem did not decrease");
            if (!first)
                rows << ',';
            first = false;
            rows << "{\"case\":\"" << mode << "\",\"strands\":" << solver.participating()
                 << ",\"initialObjective\":" << std::setprecision(15) << initialEnergy
                 << ",\"final\":" << finalDiagnostic << "}";
        }
    UploadList keep;
    ctx.submit([&](auto *list) { solver.resetCurrent(ctx.device, list, keep); });
    state.verifyImmutable(ctx.device, ctx.submit, true);
    auto csvPath = ctx.report;
    csvPath.replace_extension("csv");
    write(csvPath, csv.str());
    std::ostringstream result;
    result << std::setprecision(17) << "{\"test\":\"window-groom\",\"maxCheckpointGrowth\":" << maxCheckpointGrowth
           << ",\"maxRestPositionDrift\":" << maxRestPositionDrift
           << ",\"maxRestOrientationDrift\":" << maxRestOrientationDrift << ",\"attemptedP\":" << attemptedP
           << ",\"executedP\":" << executedP << ",\"attemptedQ\":" << attemptedQ << ",\"executedQ\":" << executedQ
           << ",\"ordinary\":" << ordinary << ",\"fallback\":" << fallback << ",\"hard\":" << hard
           << ",\"failures\":" << failures << ",\"resetExact\":true,\"cases\":[" << rows.str() << "]}";
    write(ctx.report, result.str());
}
