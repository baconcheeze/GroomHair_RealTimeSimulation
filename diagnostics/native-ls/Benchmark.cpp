#include "../joint/Problem.h"
#include "Allocation.h"
#include "Fixture.h"
#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <new>
using hair::D;
thread_local bool measureAllocations = false;
thread_local uint64_t allocationCount = 0;
void *operator new(size_t n)
{
    if (measureAllocations)
        allocationCount++;
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](size_t n)
{
    return ::operator new(n);
}
void operator delete(void *p) noexcept
{
    std::free(p);
}
void operator delete[](void *p) noexcept
{
    std::free(p);
}
void operator delete(void *p, size_t) noexcept
{
    std::free(p);
}
void operator delete[](void *p, size_t) noexcept
{
    std::free(p);
}
std::string timings(const nativels::Timing &);
void write(const fs::path &, const std::string &);
double guessEnergy(const GuideCpuState &, const windowmath::State &, const std::vector<hair::D> &,
                   const windowmath::Settings &);
std::string distributions(const std::vector<double> &input)
{
    auto v = input;
    std::sort(v.begin(), v.end());
    std::ostringstream o;
    o << std::setprecision(17) << "{\"median\":" << v[v.size() / 2]
      << ",\"P95\":" << v[size_t(std::ceil(.95 * v.size())) - 1] << '}';
    return o.str();
}
struct ComponentSamples
{
    std::array<std::vector<double>, 8> v;
    ComponentSamples()
    {
        for (auto &a : v)
            a.reserve(9);
    }
    void add(const nativels::Timing &t)
    {
        double a[] = {t.rows, t.bands, t.factor, t.initial, t.residual, t.corrections, t.trial, t.nonlinear};
        for (unsigned i = 0; i < 8; i++)
            v[i].push_back(a[i]);
    }
    std::string json() const
    {
        const char *names[] = {"rows", "bands", "factor", "initial", "residual", "corrections", "trial", "nonlinear"};
        std::string o = "{";
        for (unsigned i = 0; i < 8; i++)
        {
            if (i)
                o += ',';
            o += '"' + std::string(names[i]) + "\":" + distributions(v[i]);
        }
        return o + "}";
    }
};
int benchmark(const fs::path &input, const fs::path &out)
{
    Fixture f(input / "primary__original_initial.json");
    auto c = dummy(f);
    windowmath::Settings settings;
    settings.pinX = f.mx[0] < 0;
    settings.pinQ = f.mq[0] < 0;
    nativels::Workspace w(c, settings);
    import(w, f);
    std::ostringstream o;
    o << std::setprecision(17) << "{\"warmup\":2,\"samples\":9,\"frozenN\":" << w.n
      << ",\"frozenWorkspaceBytes\":" << w.bytes() << ",\"linear\":[";
    bool first = true;
    for (auto mode : {nativels::Mode::NativeDouble, nativels::Mode::NativeExtended})
        for (unsigned count : {0u, 1u, 2u, 3u, 5u})
        {
            std::vector<double> samples;
            samples.reserve(9);
            uint64_t allocations = 0;
            nativels::Timing last;
            ComponentSamples components;
            for (unsigned k = 0; k < 11; k++)
            {
                w.timing = {};
                allocationCount = 0;
                measureAllocations = true;
                auto t = nativels::Clock::now();
                bool valid = w.solve(count, mode);
                double wall = nativels::ms(t);
                measureAllocations = false;
                require(valid, "Benchmark factor failure");
                if (k >= 2)
                {
                    samples.push_back(wall);
                    allocations = std::max(allocations, allocationCount);
                    components.add(w.timing);
                }
                last = w.timing;
            }
            if (!first)
                o << ',';
            first = false;
            o << "{\"mode\":\"" << (mode == nativels::Mode::NativeDouble ? "NativeDouble" : "NativeExtended")
              << "\",\"corrections\":" << count << ",\"wallMs\":" << distributions(samples)
              << ",\"maxAllocations\":" << allocations << ",\"componentsMs\":" << components.json()
              << ",\"lastSampleTiming\":" << timings(last) << '}';
        }
    o << "],\"nonlinear\":[";
    auto p = jointcpu::loadProblem(input / "primary.jwp");
    nativels::Workspace live(p->cpu, p->set);
    auto state = p->initial;
    first = true;
    for (unsigned count : {0u, 1u, 2u, 3u, 5u})
    {
        std::vector<double> samples;
        samples.reserve(9);
        uint64_t allocations = 0;
        nativels::Timing last;
        ComponentSamples components;
        unsigned accepted = 0;
        for (unsigned k = 0; k < 11; k++)
        {
            state = p->initial;
            live.timing = {};
            allocationCount = 0;
            measureAllocations = true;
            auto t = nativels::Clock::now();
            bool valid = true;
            accepted = 0;
            for (auto [pt, qt] :
                 std::array<std::pair<double, double>, 3>{{{1e-11, 1e-9}, {1e-12, 1e-10}, {1e-14, 1e-13}}})
            {
                auto r = nativels::minimize(live, p->cpu, state, p->dy, p->set, count, nativels::Mode::NativeExtended,
                                            pt, qt);
                valid &= r.diagnostics.qualified;
                accepted += r.accepted;
            }
            double wall = nativels::ms(t);
            measureAllocations = false;
            require(valid, "Nonlinear timing qualification failed");
            if (k >= 2)
            {
                samples.push_back(wall);
                allocations = std::max(allocations, allocationCount);
                components.add(live.timing);
            }
            last = live.timing;
        }
        double rms = 0, mx = 0, qerror = 0;
        for (unsigned j = 0; j < state.x.size(); j++)
        {
            double d = (state.x[j] - p->reference.x[j]).norm();
            rms += d * d;
            mx = std::max(mx, d);
        }
        for (unsigned j = 0; j < state.q.size(); j++)
            qerror = std::max(qerror, hair::angle(state.q[j], p->reference.q[j]));
        if (!first)
            o << ',';
        first = false;
        o << "{\"F\":" << nativels::objective(p->cpu, state, p->dy, p->set, live.layout)
          << ",\"positionRMS\":" << std::sqrt(rms / state.x.size()) << ",\"positionMax\":" << mx
          << ",\"quaternionMax\":" << qerror << ",\"corrections\":" << count << ",\"accepted\":" << accepted
          << ",\"wallMs\":" << distributions(samples) << ",\"maxAllocations\":" << allocations
          << ",\"componentsMs\":" << components.json() << ",\"lastSampleTiming\":" << timings(last) << '}';
    }
    o << "],\"dynamicStep\":";
    auto rest = windowmath::rest(p->cpu);
    auto velocity = std::vector<D>(rest.x.size());
    auto target = velocity, updated = velocity;
    auto inertial = rest;
    double h = p->set.h;
    D gravity{-9.699665069580078, .06558697670698166, -1.4657117128372192};
    std::vector<double> samples;
    samples.reserve(9);
    uint64_t allocations = 0;
    for (unsigned k = 0; k < 11; k++)
    {
        state = rest;
        std::fill(velocity.begin(), velocity.end(), D{});
        allocationCount = 0;
        measureAllocations = true;
        auto t = nativels::Clock::now();
        for (unsigned j = 0; j < state.x.size(); j++)
        {
            target[j] = j == 0 ? rest.x[j] : rest.x[j] + h * velocity[j] + h * h * gravity;
            state.x[j] = target[j];
        }
        inertial = rest;
        if (guessEnergy(p->cpu, inertial, target, p->set) < guessEnergy(p->cpu, state, target, p->set))
            state = inertial;
        auto result = nativels::minimize(live, p->cpu, state, target, p->set);
        for (unsigned j = 0; j < state.x.size(); j++)
            updated[j] = j == 0 ? D{} : (state.x[j] - rest.x[j]) / h;
        double wall = nativels::ms(t);
        measureAllocations = false;
        require(result.diagnostics.qualified, "Dynamic timing qualification failed");
        if (k >= 2)
        {
            samples.push_back(wall);
            allocations = std::max(allocations, allocationCount);
        }
    }
    o << "{\"wallMs\":" << distributions(samples) << ",\"maxAllocations\":" << allocations << "},\"storage\":[";
    first = true;
    for (unsigned n : {2u, 5u, 32u, 64u, 128u})
    {
        GuideCpuState cpu;
        cpu.masses.resize(n);
        cpu.lengths.resize(n - 1);
        cpu.bends.resize(n - 2);
        cpu.ranges = {{0, n, 0, n - 1, 0, n - 2}};
        nativels::Workspace workspace(cpu, p->set);
        if (!first)
            o << ',';
        first = false;
        o << "{\"vertices\":" << n << ",\"DOFs\":" << workspace.n << ",\"workspaceBytes\":" << workspace.bytes()
          << ",\"stateTargetVelocityHistoryPayloadBytes\":" << (6 * size_t(n) - 1) * sizeof(D)
          << ",\"factorPayloadBytes\":" << 2 * n * sizeof(nativels::Mat)
          << ",\"rowCapacity\":" << workspace.raw.capacity() + workspace.scaled.capacity() << '}';
    }
    o << "]}";
    write(out / "benchmark.json", o.str());
    return 0;
}
