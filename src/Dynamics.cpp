#include "Dynamics.h"
#include "OrientationFixtures.h"
#include <fstream>
#include <sstream>
using Microsoft::WRL::ComPtr;
namespace
{
ComPtr<ID3D12Resource> readbackBuffer(ID3D12Device *device, size_t bytes)
{
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = bytes;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    guidegpu::check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST,
                                                    nullptr, IID_PPV_ARGS(&r)));
    return r;
}
bool same(const windowmath::Settings &a, const windowmath::Settings &b)
{
    return a.h == b.h && a.ss == b.ss && a.bt == b.bt && a.participating == b.participating && a.pinX == b.pinX &&
           a.pinQ == b.pinQ;
}
} // namespace
void DynamicsTiming::initialize(ID3D12Device *d, ID3D12CommandQueue *queue)
{
    hair::require(queue != nullptr, "GPU timing queue unavailable");
    guidegpu::check(queue->GetTimestampFrequency(&frequency));
    D3D12_QUERY_HEAP_DESC desc{};
    desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    desc.Count = 6;
    guidegpu::check(d->CreateQueryHeap(&desc, IID_PPV_ARGS(&heap)));
    readback = readbackBuffer(d, 48);
}
std::array<double, 3> DynamicsTiming::milliseconds() const
{
    auto bytes = guidegpu::mappedBytes(readback.Get(), 48);
    uint64_t ticks[6];
    std::memcpy(ticks, bytes.data(), 48);
    std::array<double, 3> r{};
    for (unsigned i = 0; i < 3; i++)
        r[i] = double(ticks[2 * i + 1] - ticks[2 * i]) * 1000 / frequency;
    return r;
}
void DynamicsSolver::initialize(ID3D12Device *d, ID3D12GraphicsCommandList *list, guidegpu::State &shared,
                                WindowSolver &w, const std::filesystem::path &path, UploadList &keep)
{
    state = &shared;
    window = &w;
    for (auto *resource :
         {state->position.resource.Get(), state->velocity.resource.Get(), window->targetBuffer().resource.Get()})
        hair::require((resource->GetDesc().Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) != 0,
                      "Dynamics requires authoritative UAV buffers");
    previous.create(d, cpu().source->positions.size() * 12, true, L"DynamicsPreviousPosition");
    acceleration.create(d,cpu().source->positions.size()*12,true,L"DynamicsRealizedAcceleration");
    acceleration.upload(d,list,nullptr,0,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    status.create(d, cpu().ranges.size() * 48, true, L"DynamicsStickyStatus");
    previous.upload(d, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    status.upload(d, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    for (auto &r : statusReadback)
        r = readbackBuffer(d, status.bytes);
    D3D12_ROOT_PARAMETER p[15]{};
    p[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    p[0].Constants.Num32BitValues = 16;
    for (unsigned i = 1; i < 15; i++)
    {
        p[i].ParameterType = i <= 5 ? D3D12_ROOT_PARAMETER_TYPE_SRV : D3D12_ROOT_PARAMETER_TYPE_UAV;
        p[i].Descriptor.ShaderRegister = i <= 5 ? i - 1 : i - 6;
    }
    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 15;
    rs.pParameters = p;
    ComPtr<ID3DBlob> b, e;
    guidegpu::check(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &b, &e));
    guidegpu::check(d->CreateRootSignature(0, b->GetBufferPointer(), b->GetBufferSize(), IID_PPV_ARGS(&root)));
    auto load = [&](const char *name, ComPtr<ID3D12PipelineState> &ps) {
        std::ifstream f(path / name, std::ios::binary | std::ios::ate);
        hair::require(bool(f), "Missing dynamics DXIL");
        auto n = f.tellg();
        std::vector<char> code(size_t(n), 0);
        f.seekg(0);
        hair::require(bool(f.read(code.data(), n)), "Invalid dynamics DXIL");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get();
        desc.CS = {code.data(), code.size()};
        guidegpu::check(d->CreateComputePipelineState(&desc, IID_PPV_ARGS(&ps)));
    };
    load("BeginSubstep.dxil", beginPipeline);
    load("EndSubstep.dxil", endPipeline);
    load("KickVelocity.dxil", kickPipeline);
}
bool DynamicsSolver::ready() const
{
    if (configured && (!same(settings, preparedSettings) || std::memcmp(&gravity, &preparedGravity, 12) != 0 ||
                       sweeps != preparedSweeps || initializer!=preparedInitializer || windowSchedule!=preparedSchedule))
        configured = false;
    return configured && !failed;
}
void DynamicsSolver::reset(ID3D12Device *d, ID3D12GraphicsCommandList *list, UploadList &keep)
{
    window->settings = settings;window->schedule=windowSchedule;
    window->validateSettings();
    hair::require(initializer<=3 && sweeps > 0 && sweeps <= 4096 && std::isfinite(gravity.x) && std::isfinite(gravity.y) &&
                      std::isfinite(gravity.z),
                  "Invalid dynamics configuration");
    window->resetCurrent(d, list, keep);
    status.upload(d, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    previous.upload(d, list, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    preparedSettings = settings;
    ++resetGeneration;
    preparedGravity = gravity;
    preparedSweeps = sweeps;preparedInitializer=initializer;preparedSchedule=windowSchedule;
    acceleration.upload(d,list,nullptr,0,keep,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    configured = true;
    inside = false;
    failed = false;
    failure.clear();
    queuedSteps = observedCompleted = 0;
    pollPending.fill(false);
    metricsLastStep = false;
    velocityChangedSinceEnd = false;
}
void DynamicsSolver::bind(ID3D12GraphicsCommandList *list, Float3 kick)
{
    list->SetComputeRootSignature(root.Get());
    DynamicsParameters p{unsigned(cpu().ranges.size()),
                         window->participating(),
                         settings.pinX ? 1u : 0u,
                         settings.pinQ ? 1u : 0u,
                         settings.h,
                         gravity.x,
                         gravity.y,
                         gravity.z,
                         queuedSteps + 1,
                         initializer,
                         0,
                         0,
                         kick.x,
                         kick.y,
                         kick.z,
                         settings.ss};
    list->SetComputeRoot32BitConstants(0, 16, &p, 0);
    guidegpu::Buffer *in[] = {&state->ranges, &state->restPosition, &state->restOrientation, &state->mass, &state->restLength};
    for (unsigned i = 0; i < 5; i++)
        list->SetComputeRootShaderResourceView(i + 1, in[i]->address());
    guidegpu::Buffer *out[] = {&state->position,        &state->orientation,    &state->velocity,       &previous,
                               &window->targetBuffer(), &window->faultBuffer(), &window->statsBuffer(), &status, &acceleration};
    for (unsigned i = 0; i < 9; i++)
    {
        out[i]->transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        list->SetComputeRootUnorderedAccessView(i + 6, out[i]->address());
    }
}
void DynamicsSolver::barriers(ID3D12GraphicsCommandList *list)
{
    for (auto *b : {&state->position, &state->orientation, &state->velocity, &previous, &window->targetBuffer(),
                    &window->faultBuffer(), &window->statsBuffer(), &status, &acceleration})
        guidegpu::uavBarrier(list, b->resource.Get());
}
void DynamicsSolver::begin(ID3D12GraphicsCommandList *list)
{
    hair::require(ready() && !inside && queuedSteps < UINT32_MAX - 1,
                  "Dynamics unconfigured/stale/failed or substep overflow; Reset Simulation");
    window->settings = settings;window->schedule=windowSchedule;
    inside = true;
    bind(list);
    list->SetPipelineState(beginPipeline.Get());
    list->Dispatch((window->participating() + 63) / 64, 1, 1);
    barriers(list);
    window->targetBuffer().transition(list, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
void DynamicsSolver::solve(ID3D12GraphicsCommandList *list, bool snapshots)
{
    hair::require(inside, "No dynamic substep");
    metricsLastStep = snapshots || diagnosticSnapshots;
    window->runDynamic(list, sweeps, metricsLastStep);
}
void DynamicsSolver::end(ID3D12GraphicsCommandList *list)
{
    hair::require(inside, "No dynamic substep");
    bind(list);
    list->SetPipelineState(endPipeline.Get());
    list->Dispatch((window->participating() + 63) / 64, 1, 1);
    barriers(list);
    inside = false;
    velocityChangedSinceEnd = false;
    ++queuedSteps;
}
void DynamicsSolver::step(ID3D12GraphicsCommandList *list, unsigned count, DynamicsTiming *timer)
{
    hair::require(count > 0 && count <= 10000 && (!timer || count == 1), "Bounded dynamic batch expected");
    for (unsigned i = 0; i < count; i++)
    {
        auto stamp = [&](unsigned index) {
            if (timer)
                list->EndQuery(timer->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, index);
        };
        stamp(0);
        begin(list);
        stamp(1);
        stamp(2);
        solve(list);
        stamp(3);
        stamp(4);
        end(list);
        stamp(5);
        if (timer)
            list->ResolveQueryData(timer->heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 6, timer->readback.Get(), 0);
    }
}
void DynamicsSolver::kick(ID3D12GraphicsCommandList *list, Float3 amplitude)
{
    hair::require(ready() && !inside && std::isfinite(amplitude.x) && std::isfinite(amplitude.y) &&
                      std::isfinite(amplitude.z),
                  "Invalid one-shot velocity perturbation");
    velocityChangedSinceEnd = true;
    window->settings = settings;window->schedule=windowSchedule;
    bind(list, amplitude);
    list->SetPipelineState(kickPipeline.Get());
    list->Dispatch((window->participating() + 63) / 64, 1, 1);
    barriers(list);
}
void DynamicsSolver::recordStatusPoll(ID3D12GraphicsCommandList *list, unsigned slot)
{
    status.transition(list, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(statusReadback.at(slot).Get(), 0, status.resource.Get(), 0, status.bytes);
    status.transition(list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    pollPending[slot] = true;
}
void DynamicsSolver::observe(unsigned slot)
{
    if (!pollPending.at(slot))
        return;
    pollPending[slot] = false;
    auto raw = guidegpu::mappedBytes(statusReadback[slot].Get(), status.bytes);
    auto *data = reinterpret_cast<const DynamicsStatus *>(raw.data());
    unsigned n = settings.participating ? std::min(settings.participating, unsigned(cpu().ranges.size()))
                                        : unsigned(cpu().ranges.size());
    observedCompleted = UINT32_MAX;
    for (unsigned i = 0; i < n; i++)
    {
        auto s = data[i];
        observedCompleted = std::min(observedCompleted, s.completed);
        if (s.code)
        {
            failed = true;
            failure = "Substep " + std::to_string(s.substep) + " strand " + std::to_string(i) + " operation " +
                      std::to_string(s.operation) + " code " + std::to_string(s.code);
        }
    }
}
DynamicsSnapshot DynamicsSolver::read(ID3D12Device *d, const Submit &submit)
{
    DynamicsSnapshot s;
    s.guide = window->read(d, submit);
    s.velocity = guidegpu::read<Float3>(state->velocity, d, submit, cpu().velocities.size());
    s.previous = guidegpu::read<Float3>(previous, d, submit, cpu().velocities.size());
    s.target = guidegpu::read<Float3>(window->targetBuffer(), d, submit, cpu().velocities.size());
    s.status = guidegpu::read<DynamicsStatus>(status, d, submit, cpu().ranges.size());
    return s;
}
std::string DynamicsSolver::diagnostics(ID3D12Device *d, const Submit &submit, const DynamicsSnapshot &s)
{
    ready();
    hair::require(configured&&!inside,"Cannot diagnose stale/incomplete dynamics; Reset Simulation");
    state->verifyImmutable(d, submit, false, true, true);
    unsigned n = settings.participating ? std::min(settings.participating, unsigned(cpu().ranges.size()))
                                        : unsigned(cpu().ranges.size());
    double bx = 0, bq = 0, lx = 0, lq = 0;
    double speed = 0, kinetic = 0, rootX = 0, rootQ = 0, strain = 0, tip = 0, displacement = 0, correction = 0,
           velocityError = 0, ssError = 0, qDrift = 0, xDrift = 0;
    uint64_t ordinary = 0, fallback = 0, hard = 0;
    auto rest = windowmath::rest(cpu());
    for (unsigned k = 0; k < cpu().ranges.size(); k++)
    {
        auto r = cpu().ranges[k];
        auto statusValue = s.status[k];
        hair::require(statusValue.code == 0 && s.guide.faults[k].code == 0,
                      "Dynamics failure substep=" + std::to_string(statusValue.substep) +
                          " strand=" + std::to_string(k) + " operation=" + std::to_string(statusValue.operation) +
                          " code=" + std::to_string(statusValue.code));
        if (k >= n)
        {
            for (unsigned j = 0; j < r.vertexCount; j++)
            {
                unsigned v = r.firstVertex + j;
                hair::require((s.guide.state.x[v] - rest.x[v]).norm() == 0 &&
                                  hair::D{s.velocity[v].x, s.velocity[v].y, s.velocity[v].z}.norm() == 0,
                              "Inactive dynamics state changed");
            }
            for (unsigned j = 0; j < r.segmentCount; j++)
                hair::require((s.guide.state.q[r.firstSegment + j] - rest.q[r.firstSegment + j]).norm() == 0,
                              "Inactive orientation changed");
            continue;
        }
        hair::require(statusValue.completed == queuedSteps && statusValue.beginCount == queuedSteps &&
                          statusValue.endCount == queuedSteps,
                      "Incomplete dynamic substep");
        auto ws = s.guide.stats[k];
        bx = std::max(bx, double(ws.batchX));
        bq = std::max(bq, 4 * std::asin(std::min(1., double(ws.batchQ) * .5)));
        lx = std::max(lx, double(ws.lastX));
        lq = std::max(lq, 4 * std::asin(std::min(1., double(ws.lastQ) * .5)));
        ordinary += statusValue.ordinary;
        fallback += statusValue.fallback;
        hard += statusValue.hard;
        if (settings.pinX)
        {
            rootX = std::max(rootX, (s.guide.state.x[r.firstVertex] - rest.x[r.firstVertex]).norm());
            hair::require(
                hair::D{s.velocity[r.firstVertex].x, s.velocity[r.firstVertex].y, s.velocity[r.firstVertex].z}.norm() ==
                    0,
                "Root velocity changed");
        }
        if (settings.pinQ)
            rootQ = std::max(rootQ, (s.guide.state.q[r.firstSegment] - rest.q[r.firstSegment]).norm());
        for (unsigned j = 0; j < r.vertexCount; j++)
        {
            unsigned v = r.firstVertex + j;
            hair::D velocity{s.velocity[v].x, s.velocity[v].y, s.velocity[v].z},
                old{s.previous[v].x, s.previous[v].y, s.previous[v].z},
                target{s.target[v].x, s.target[v].y, s.target[v].z};
            hair::require(std::isfinite(velocity.norm()), "Nonfinite velocity");
            speed = std::max(speed, velocity.norm());
            kinetic += .5 * cpu().masses[v] * velocity.dot(velocity);
            if (queuedSteps)
            {
                displacement = std::max(displacement, (s.guide.state.x[v] - old).norm());
                correction = std::max(correction, (s.guide.state.x[v] - target).norm());
                velocityError =
                    std::max(velocityError, (velocity - (s.guide.state.x[v] - old) / double(settings.h)).norm());
            }
        }
        for (unsigned j = 0; j < r.segmentCount; j++)
        {
            unsigned v = r.firstVertex + j;
            strain = std::max(
                strain,
                std::abs((s.guide.state.x[v + 1] - s.guide.state.x[v]).norm() / cpu().lengths[r.firstSegment + j] - 1));
        }
        for (unsigned j = 0; j < r.segmentCount; j++)
        {
            unsigned v = r.firstVertex + j, segment = r.firstSegment + j;
            auto css = (s.guide.state.x[v + 1] - s.guide.state.x[v]) / double(cpu().lengths[segment]) -
                       hair::rotate(s.guide.state.q[segment], hair::D{0, 0, 1});
            ssError = std::max(ssError, css.norm());
            qDrift = std::max(qDrift, hair::angle(s.guide.state.q[segment], rest.q[segment]));
        }
        for (unsigned j = 0; j < r.vertexCount; j++)
            xDrift = std::max(xDrift, (s.guide.state.x[r.firstVertex + j] - rest.x[r.firstVertex + j]).norm());
        unsigned t = r.firstVertex + r.vertexCount - 1;
        tip = std::max(tip, (s.guide.state.x[t] - rest.x[t]).norm());
    }
    hair::require(rootX == 0 && rootQ == 0, "Pinned dynamic boundary changed");
    auto energy = queuedSteps ? windowmath::measure(cpu(), s.guide.state, s.target, settings)
                              : windowmath::measure(cpu(), s.guide.state, cpu().source->positions, settings);
    hair::require(std::isfinite(energy.total()) && energy.norm < 2e-5, "Invalid dynamic diagnostic");
    auto number = [](double value, bool available) {
        if (!available)
            return std::string("null");
        std::ostringstream n;
        n.precision(15);
        n << value;
        return n.str();
    };
    std::string objectiveBefore = "null";
    if (metricsLastStep && queuedSteps)
    {
        auto initial = s.guide.state;
        auto baseline = guidegpu::read<hair::F>(window->batchQuaternionBuffer(), d, submit, cpu().orientations.size());
        for (size_t i = 0; i < initial.q.size(); i++)
            initial.q[i] = hair::D(baseline[i]);
        auto initialPositions=guidegpu::read<Float3>(window->batchPositionBuffer(),d,submit,cpu().source->positions.size());
        for (size_t i = 0; i < initial.x.size(); i++)
            initial.x[i] = {initialPositions[i].x,initialPositions[i].y,initialPositions[i].z};
        double before = windowmath::measure(cpu(), initial, s.target, settings).total();
        hair::require(energy.total() <= before + std::max(1e-11, before * 1e-6), "Within-substep objective increased");
        objectiveBefore = number(before, true);
    }
    std::ostringstream out;
    out.precision(15);
    out << "{\"queuedSubsteps\":" << queuedSteps << ",\"completedSubsteps\":" << queuedSteps
        << ",\"simulationTime\":" << simulationTime() << ",\"initializer\":" << initializer << ",\"h\":" << settings.h << ",\"sweepsPerSubstep\":" << sweeps
        << ",\"windowSweepsRequested\":" << uint64_t(queuedSteps) * sweeps << ",\"participatingStrands\":" << n
        << ",\"gravitySolver\":[" << gravity.x << ',' << gravity.y << ',' << gravity.z
        << "],\"lastSubstepObjective\":" << energy.json() << ",\"objectiveBeforeWindowSweeps\":" << objectiveBefore
        << ",\"solverBatchPositionUpdate\":" << number(bx, metricsLastStep)
        << ",\"solverLastSweepPositionUpdate\":" << number(lx, metricsLastStep)
        << ",\"solverBatchOrientationUpdate\":" << number(bq, metricsLastStep)
        << ",\"solverLastSweepOrientationUpdate\":" << number(lq, metricsLastStep) << ",\"maximumSpeed\":" << speed
        << ",\"maximumShearStretchError\":" << ssError << ",\"maxPositionFromRest\":" << xDrift
        << ",\"maxQuaternionFromRest\":" << qDrift << ",\"elasticEnergy\":" << energy.ss + energy.bt
        << ",\"kineticEnergy\":" << kinetic << ",\"maximumAxialStrain\":" << strain << ",\"maxTipDisplacement\":" << tip
        << ",\"physicalSubstepDisplacement\":" << displacement << ",\"correctionFromPrediction\":" << correction
        << ",\"velocityReconstructionError\":" << number(velocityError, !velocityChangedSinceEnd)
        << ",\"rootPositionError\":" << rootX << ",\"rootQuaternionError\":" << rootQ << ",\"ordinary\":" << ordinary
        << ",\"fallback\":" << fallback << ",\"hard\":" << hard
        << ",\"failures\":0,\"restChanges\":0,\"inactiveChanges\":0}";
    return out.str();
}
