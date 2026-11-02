#include "WindowSolver.h"
#include "OrientationFixtures.h"
#include <fstream>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace
{
bool same(const windowmath::Settings &a, const windowmath::Settings &b)
{
    return a.participating == b.participating && a.h == b.h && a.ss == b.ss && a.bt == b.bt && a.pinX == b.pinX &&
           a.pinQ == b.pinQ;
}
ComPtr<ID3D12PipelineState> loadPipeline(ID3D12Device *device, ID3D12RootSignature *root,
                                         const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    hair::require(bool(file), "Missing compiled window shader: " + path.string());
    auto size = file.tellg();
    std::vector<char> code(size_t(size), 0);
    file.seekg(0);
    hair::require(bool(file.read(code.data(), size)), "Window shader read failure");
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = root;
    desc.CS = {code.data(), code.size()};
    ComPtr<ID3D12PipelineState> result;
    guidegpu::check(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&result)));
    return result;
}
} // namespace
bool WindowSolver::ready() const
{
    if (prepared && (!same(settings, preparedSettings)||schedule!=preparedSchedule))
        prepared = false;
    return prepared;
}
unsigned WindowSolver::participating() const
{
    return settings.participating ? std::min(settings.participating, unsigned(cpu().ranges.size()))
                                  : unsigned(cpu().ranges.size());
}
WindowParameters WindowSolver::parameters(unsigned iterations, bool first, unsigned base, unsigned flags,
                                          unsigned limit) const
{
    return {unsigned(cpu().ranges.size()),
            unsigned(cpu().source->positions.size()),
            unsigned(cpu().orientations.size()),
            unsigned(cpu().bends.size()),
            participating(),
            iterations,
            settings.pinX ? 1u : 0u,
            settings.pinQ ? 1u : 0u,
            settings.ss,
            settings.bt,
            1.f / (settings.h * settings.h),
            first ? 1u : 0u,
            base,
            limit,
            flags|(schedule==1?8u:schedule==2?16u:0u),
            0};
}
void WindowSolver::initialize(ID3D12Device *device, ID3D12GraphicsCommandList *list, guidegpu::State &shared,
                              const std::filesystem::path &shaders, UploadList &keep)
{
    state = &shared;
    hair::require((state->position.resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
                  "Authoritative GuideCurrentPosition is not UAV-capable");
    D3D12_ROOT_PARAMETER params[16]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 16;
    for (unsigned i = 1; i < 16; ++i)
    {
        params[i].ParameterType = i <= 7 ? D3D12_ROOT_PARAMETER_TYPE_SRV : D3D12_ROOT_PARAMETER_TYPE_UAV;
        params[i].Descriptor.ShaderRegister = i <= 7 ? i - 1 : i - 8;
    }
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 16;
    rs.pParameters = params;
    ComPtr<ID3DBlob> blob, error;
    guidegpu::check(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error));
    guidegpu::check(
        device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)));
    pipeline = loadPipeline(device, root.Get(), shaders / "WindowSolve.dxil");
    perturbPipeline = loadPipeline(device, root.Get(), shaders / "WindowPerturb.dxil");
    auto create = [&](guidegpu::Buffer &b, size_t bytes, bool uav, const wchar_t *name) {
        b.create(device, bytes, uav, name);
        b.upload(device, list, nullptr, 0, keep,
                 uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    size_t v = cpu().source->positions.size(), s = cpu().orientations.size(), n = cpu().ranges.size();
    target.create(device, v * 12, true, L"WindowInertialTarget");
    target.upload(device, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    create(faults, n * sizeof(WindowFault), true, L"WindowStickyFault");
    create(stats, n * sizeof(WindowStats), true, L"WindowRunStats");
    create(batchX, v * 12, true, L"WindowBatchPositionSnapshot");
    create(batchQ, s * 16, true, L"WindowBatchQuaternionSnapshot");
    create(sweepX, v * 12, true, L"WindowSweepPositionSnapshot");
    create(sweepQ, s * 16, true, L"WindowSweepQuaternionSnapshot");
}
void WindowSolver::prepare(ID3D12Device *device, ID3D12GraphicsCommandList *list, const std::vector<Float3> &y,
                           const std::string &mode, UploadList &keep)
{
    hair::require(y.size() == cpu().source->positions.size() && std::isfinite(settings.h) && settings.h > 0 &&
                      std::isfinite(1.f / (settings.h * settings.h)) && 1.f / (settings.h * settings.h) > 0 &&
                      std::isfinite(settings.ss) && settings.ss > 0 && std::isfinite(settings.bt) && settings.bt > 0,
                  "Invalid fixed problem coefficients/target size");
    for (auto p : y)
        hair::require(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z), "Nonfinite inertial target");
    if (settings.pinX)
        for (auto r : cpu().ranges)
            hair::require(std::memcmp(&y[r.firstVertex], &cpu().source->positions[r.firstVertex], 12) == 0,
                          "Pinned target must match fixed rest root"); // Preparation snapshots diagnose inactive DOFs
                                                                       // without assuming they started at rest.
    if (!preparedX.resource)
    {
        preparedX.create(device, cpu().source->positions.size() * 12, false, L"WindowPreparedPositionSnapshot");
        preparedQ.create(device, cpu().orientations.size() * 16, false, L"WindowPreparedQuaternionSnapshot");
    }
    auto snapshot = [&](guidegpu::Buffer &destination, guidegpu::Buffer &source) {
        auto previous = source.state;
        source.transition(list, D3D12_RESOURCE_STATE_COPY_SOURCE);
        destination.transition(list, D3D12_RESOURCE_STATE_COPY_DEST);
        list->CopyBufferRegion(destination.resource.Get(), 0, source.resource.Get(), 0, source.bytes);
        source.transition(list, previous);
        destination.transition(list, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    };
    snapshot(preparedX, state->position);
    snapshot(preparedQ, state->orientation);
    fixedTargets = y;
    targetMode = mode;
    preparedSettings = settings;preparedSchedule=schedule;
    prepared = true;
    requestedSweeps = 0;
    hasRun = false;
    ++generation;
    target.upload(device, list, y.data(), y.size() * 12, keep, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    faults.upload(device, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    stats.upload(device, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
void WindowSolver::resetCurrent(ID3D12Device *device, ID3D12GraphicsCommandList *list, UploadList &keep)
{
    state->position.upload(device, list, cpu().source->positions.data(), cpu().source->positions.size() * 12, keep,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    state->reset(device, list, cpu().orientations, keep);
    state->velocity.upload(device, list, cpu().velocities.data(), cpu().velocities.size() * 12, keep,
                           D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    faults.upload(device, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    stats.upload(device, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    invalidate();
    requestedSweeps = 0;
    hasRun = false;
}
void WindowSolver::bind(ID3D12GraphicsCommandList *list)
{
    state->position.transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    state->orientation.transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    list->SetComputeRootSignature(root.Get());
    guidegpu::Buffer *inputs[] = {&state->ranges,          &state->restLength, &state->restBend,
                                  &state->restOrientation, &state->mass,       &target,
                                  &state->restPosition};
    for (unsigned i = 0; i < 7; ++i)
        list->SetComputeRootShaderResourceView(i + 1, inputs[i]->address());
    guidegpu::Buffer *outputs[] = {&state->position, &state->orientation, &faults, &stats, &batchX, &batchQ, &sweepX,
                                   &sweepQ};
    for (unsigned i = 0; i < 8; ++i)
        list->SetComputeRootUnorderedAccessView(i + 8, outputs[i]->address());
}
void WindowSolver::run(ID3D12GraphicsCommandList *list, unsigned sweeps, unsigned flags, unsigned limit, unsigned chunk)
{
    hair::require(ready(), "Window problem is unprepared or stale; explicitly PrepareProblem");
    hair::require(sweeps > 0 && sweeps <= 65536 && chunk > 0 && chunk <= 8, "Invalid bounded window sweep/chunk count");
    hair::require(uint64_t(requestedSweeps) + sweeps <= UINT32_MAX, "Sweep ordinal overflow");
    dispatch(list, sweeps, flags, limit, chunk, requestedSweeps);
    requestedSweeps += sweeps;
    hasRun = true;
}
void WindowSolver::validateSettings() const
{
    hair::require(schedule<=2&&std::isfinite(settings.h) && settings.h > 0 && std::isfinite(1.f / (settings.h * settings.h)) &&
                      1.f / (settings.h * settings.h) > 0 && std::isfinite(settings.ss) && settings.ss > 0 &&
                      std::isfinite(settings.bt) && settings.bt > 0,
                  "Invalid dynamics/window coefficients");
}
void WindowSolver::runDynamic(ID3D12GraphicsCommandList *list, unsigned sweeps, bool snapshots,unsigned baseSweep)
{
    validateSettings();
    hair::require(sweeps > 0 && sweeps <= 4096, "Invalid sweeps per dynamic substep");
    invalidate();
    targetMode = "dynamics";
    dispatch(list, sweeps, (snapshots ? 0u : 4u)|(uncachedValidation?1u:0u), 0, ChunkSweeps, baseSweep);
}
void WindowSolver::dispatch(ID3D12GraphicsCommandList *list, unsigned sweeps, unsigned flags, unsigned limit,
                            unsigned chunk, unsigned base)
{
    bind(list);
    list->SetPipelineState(pipeline.Get());
    for (unsigned done = 0; done < sweeps; done += chunk)
    {
        auto p = parameters(std::min(chunk, sweeps - done), done == 0, base + done, flags, limit);
        list->SetComputeRoot32BitConstants(0, 16, &p, 0);
        list->Dispatch((p.participating + 63) / 64, 1, 1);
        for (auto *b : {&state->position, &state->orientation, &faults, &stats, &batchX, &batchQ, &sweepX, &sweepQ})
            guidegpu::uavBarrier(list, b->resource.Get());
    }
}
void WindowSolver::perturb(ID3D12GraphicsCommandList *list, unsigned mode)
{
    hair::require(ready(), "Prepare a consistent problem before perturbation");
    hasRun = false;
    bind(list);
    list->SetPipelineState(perturbPipeline.Get());
    auto p = parameters(0, false, requestedSweeps);
    p.perturbMode = mode;
    list->SetComputeRoot32BitConstants(0, 16, &p, 0);
    list->Dispatch((p.participating + 63) / 64, 1, 1);
    guidegpu::uavBarrier(list, state->position.resource.Get());
    guidegpu::uavBarrier(list, state->orientation.resource.Get());
}
WindowSnapshot WindowSolver::read(ID3D12Device *device, const Submit &submit)
{
    ComPtr<ID3D12Resource> x, q, f, s;
    submit([&](auto *list) {
        x = state->position.readback(device, list);
        q = state->orientation.readback(device, list);
        f = faults.readback(device, list);
        s = stats.readback(device, list);
    });
    WindowSnapshot result;
    auto copy = [](auto &v, ID3D12Resource *r, size_t n) {
        v.resize(n);
        auto bytes = guidegpu::mappedBytes(r, n * sizeof(v[0]));
        if (n)
            std::memcpy(v.data(), bytes.data(), bytes.size());
    };
    std::vector<Float3> positions;
    std::vector<hair::F> quats;
    copy(positions, x.Get(), cpu().source->positions.size());
    copy(quats, q.Get(), cpu().orientations.size());
    copy(result.faults, f.Get(), cpu().ranges.size());
    copy(result.stats, s.Get(), cpu().ranges.size());
    for (auto p : positions)
        result.state.x.emplace_back(p.x, p.y, p.z);
    for (auto p : quats)
        result.state.q.emplace_back(p);
    return result;
}
std::string WindowSolver::diagnostics(ID3D12Device *device, const Submit &submit, const WindowSnapshot &snapshot)
{
    hair::require(ready(), "Cannot diagnose stale problem");
    state->verifyImmutable(device, submit, false, true);
    auto y = guidegpu::read<Float3>(target, device, submit, fixedTargets.size());
    hair::require(std::memcmp(y.data(), fixedTargets.data(), y.size() * 12) == 0, "Inertial target changed");
    uint64_t ap = 0, ep = 0, aq = 0, eq = 0, regular = 0, fallback = 0, hard = 0, rejected = 0, failed = 0;
    double bx = 0, bq = 0, lx = 0, lq = 0, px = 0, pq = 0;
    auto rest = windowmath::rest(cpu());
    windowmath::State baseline;
    for (auto p : guidegpu::read<Float3>(preparedX, device, submit, cpu().source->positions.size()))
        baseline.x.emplace_back(p.x, p.y, p.z);
    for (auto p : guidegpu::read<hair::F>(preparedQ, device, submit, cpu().orientations.size()))
        baseline.q.emplace_back(p);
    unsigned n = participating();
    for (unsigned i = 0; i < snapshot.faults.size(); ++i)
    {
        auto f = snapshot.faults[i];
        hair::require(f.code == 0, "Window failure strand=" + std::to_string(i) + " sweep=" + std::to_string(f.sweep) +
                                       " window=" + std::to_string(f.window) + " op=" + std::to_string(f.operation) +
                                       " index=" + std::to_string(f.index) + " code=" + std::to_string(f.code));
        auto r = cpu().ranges[i];
        if (i >= n)
        {
            for (unsigned j = 0; j < r.vertexCount; ++j)
                hair::require((snapshot.state.x[r.firstVertex + j] - baseline.x[r.firstVertex + j]).norm() == 0,
                              "Inactive position changed");
            for (unsigned j = 0; j < r.segmentCount; ++j)
                hair::require((snapshot.state.q[r.firstSegment + j] - baseline.q[r.firstSegment + j]).norm() == 0,
                              "Inactive quaternion changed");
            continue;
        }
        auto s = snapshot.stats[i];
        ap += s.attemptedP;
        ep += s.executedP;
        aq += s.attemptedQ;
        eq += s.executedQ;
        regular += s.ordinary;
        fallback += s.fallback;
        hard += s.hard;
        rejected += s.rejected;
        failed += s.failures;
        bx = std::max(bx, double(s.batchX));
        bq = std::max(bq, 4 * std::asin(std::min(1., double(s.batchQ) * .5)));
        lx = std::max(lx, double(s.lastX));
        lq = std::max(lq, 4 * std::asin(std::min(1., double(s.lastQ) * .5)));
        if (settings.pinX)
            px = std::max(px, (snapshot.state.x[r.firstVertex] - rest.x[r.firstVertex]).norm());
        if (settings.pinQ)
            pq = std::max(pq, hair::angle(snapshot.state.q[r.firstSegment], rest.q[r.firstSegment]));
    }
    hair::require(px == 0 && pq < 1e-6, "Pinned boundary changed");
    auto e = windowmath::measure(cpu(), snapshot.state, fixedTargets, settings);
    hair::require(std::isfinite(e.total()) && e.norm < 2e-5, "Invalid committed state diagnostics");
    auto metric = [&](double value) {
        if (!hasRun)
            return std::string("null");
        std::ostringstream text;
        text.precision(15);
        text << value;
        return text.str();
    };
    std::ostringstream out;
    out.precision(15);
    out << "{\"ProblemGeneration\":" << generation << ",\"RequestedSweepsSincePrepare\":" << requestedSweeps
        << ",\"FixedH\":" << preparedSettings.h << ",\"baseSS\":" << preparedSettings.ss
        << ",\"baseBT\":" << preparedSettings.bt << ",\"UpdateMetricsAvailable\":" << (hasRun ? "true" : "false")
        << ",\"objective\":" << e.json() << ",\"MaxBatchPositionUpdate\":" << metric(bx)
        << ",\"MaxLastSweepPositionUpdate\":" << metric(lx) << ",\"MaxBatchOrientationUpdate\":" << metric(bq)
        << ",\"MaxLastSweepOrientationUpdate\":" << metric(lq) << ",\"attemptedP\":" << ap << ",\"executedP\":" << ep
        << ",\"attemptedQ\":" << aq << ",\"executedQ\":" << eq << ",\"ordinary\":" << regular
        << ",\"fallback\":" << fallback << ",\"hard\":" << hard << ",\"rejected\":" << rejected
        << ",\"failures\":" << failed << ",\"pinnedPositionError\":" << px << ",\"pinnedOrientationError\":" << pq
        << ",\"inactiveChanges\":0,\"restChanges\":0,\"targetChanges\":0,\"velocityChanges\":0}";
    return out.str();
}
