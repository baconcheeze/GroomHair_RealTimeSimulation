#include "../joint/Problem.h"
#include "Hybrid.h"
#include <cstring>
#include <iomanip>
namespace fs = std::filesystem;
using hair::D;
using hair::require;
using State = windowmath::State;
std::string vec(D v)
{
    std::ostringstream o;
    o << std::setprecision(17) << '[' << v.x << ',' << v.y << ',' << v.z << ']';
    return o.str();
}
void write(const fs::path &p, const std::string &s)
{
    gpuls::text(p, s);
}
struct DynamicPoint
{
    State state, guess;
    std::vector<D> velocity, acceleration, target;
    double F = 0, before = 0, strain = 0, ssError = 0, kinetic = 0, speed = 0, pinError = 0, positionResidual = 0,
           qResidual = 0, wall = 0;
    unsigned iterations = 0, guessMode = 0;
    uint64_t corrections = 0, allocations = 0;
};
struct Trajectory
{
    std::vector<DynamicPoint> points;
    bool pass = true;
    unsigned completed = 0;
    double workspace = 0;
};
void reservePoint(DynamicPoint &p, unsigned n)
{
    p.state.x.resize(n);
    p.state.q.resize(n - 1);
    p.guess = p.state;
    p.velocity.resize(n);
    p.acceleration.resize(n);
    p.target.resize(n);
}
double guessEnergy(const GuideCpuState &c, const State &guess, const std::vector<D> &target,
                   const windowmath::Settings &settings)
{
    double e = 0, h = settings.h;
    for (unsigned j = 0; j < guess.x.size(); j++)
    {
        D d = guess.x[j] - target[j];
        e += .5 * (c.masses[j] / (h * h)) * d.dot(d);
        if (j < c.lengths.size())
        {
            D css = (guess.x[j + 1] - guess.x[j]) / c.lengths[j] - hair::rotate(guess.q[j], D{0, 0, 1});
            e += .5 * (double(settings.ss) * c.lengths[j]) * css.dot(css);
        }
    }
    return e;
}
Trajectory simulate(const jointcpu::Problem &p, unsigned steps, D gravity, D initialVelocity, bool tight)
{
    Trajectory t;
    t.points.resize(steps + 1);
    auto n = unsigned(p.cpu.masses.size());
    for (auto &s : t.points)
        reservePoint(s, n);
    auto rest = windowmath::rest(p.cpu);
    t.points[0].state = rest;
    for (unsigned j = 0; j < n; j++)
        t.points[0].velocity[j] = (j == 0 && p.set.pinX) ? D{} : initialVelocity;
    nativels::Workspace w(p.cpu, p.set);
    t.workspace = double(w.bytes());
    State inertial = rest;
    double h = p.set.h;
    for (unsigned k = 1; k <= steps; k++)
    {
        auto &previous = t.points[k - 1];
        auto &point = t.points[k];

        auto begin = nativels::Clock::now();
        point.state = previous.state;
        inertial = previous.state;
        for (unsigned j = 0; j < n; j++)
        {
            bool pin = j == 0 && p.set.pinX;
            D y = pin ? rest.x[j] : previous.state.x[j] + h * previous.velocity[j] + (h * h) * gravity;
            point.target[j] = y;
            point.state.x[j] = y;
            inertial.x[j] = pin ? rest.x[j] : previous.state.x[j] + h * previous.velocity[j];
        }
        if (p.set.pinQ)
        {
            point.state.q[0] = rest.q[0];
            inertial.q[0] = rest.q[0];
        }
        double full = guessEnergy(p.cpu, point.state, point.target, p.set),
               other = guessEnergy(p.cpu, inertial, point.target, p.set);
        if (std::isfinite(other) && other < full)
        {
            point.state = inertial;
            point.guessMode = 1;
        }
        point.guess = point.state;
        point.before = nativels::objective(p.cpu, point.state, point.target, p.set, w.layout);
        auto beforeCount = w.timing.refinements;
        auto result =
            nativels::hybridMinimize(w, p.cpu, point.state, point.target, p.set, gpuls::activeLaunch.corrections, nativels::Mode::NativeExtended,
                                     tight ? 1e-13 : 1e-11, tight ? 1e-11 : 1e-9);
        point.F = result.diagnostics.objective;
        point.iterations = result.accepted;
        point.corrections = w.timing.refinements - beforeCount;
        point.positionResidual = result.diagnostics.positionCorrection;
        point.qResidual = result.diagnostics.stationarity;
        for (unsigned j = 0; j < n; j++)
        {
            bool pin = j == 0 && p.set.pinX;
            point.velocity[j] = pin ? D{} : (point.state.x[j] - previous.state.x[j]) / h;
            point.acceleration[j] = pin ? D{} : (point.velocity[j] - previous.velocity[j]) / h;
            point.kinetic += .5 * p.cpu.masses[j] * point.velocity[j].dot(point.velocity[j]);
            point.speed = std::max(point.speed, point.velocity[j].norm());
        }
        point.wall = nativels::ms(begin);

        point.allocations = 0;
        bool finite = result.diagnostics.qualified && !result.factorFailure;
        for (auto x : point.state.x)
            finite &= std::isfinite(x.norm());
        for (auto v : point.velocity)
            finite &= std::isfinite(v.norm());
        for (auto q : point.state.q)
            finite &= std::isfinite(q.norm()) && std::abs(q.norm() - 1) < 2e-4;
        for (unsigned j = 0; j < p.cpu.lengths.size(); j++)
        {
            D e = (point.state.x[j + 1] - point.state.x[j]) / p.cpu.lengths[j];
            point.strain = std::max(point.strain, std::abs(e.norm() - 1));
            point.ssError = std::max(point.ssError, (e - hair::rotate(point.state.q[j], D{0, 0, 1})).norm());
        }
        if (p.set.pinX)
        {
            point.pinError = (point.state.x[0] - rest.x[0]).norm();
            finite &= point.pinError == 0 && point.velocity[0].norm() == 0;
        }
        if (p.set.pinQ)
            finite &= (point.state.q[0] - rest.q[0]).norm() == 0;
        finite &= point.F <= point.before + 1e-22 + 1e-10 * std::abs(point.before);
        t.pass &= finite;
        t.completed = k;
        if (!finite)
            break;
    }
    return t;
}
std::string pointsJSON(const Trajectory &t)
{
    std::ostringstream o;
    o << std::setprecision(17) << "{\"pass\":" << (t.pass ? "true" : "false") << ",\"completed\":" << t.completed
      << ",\"workspaceBytes\":" << t.workspace << ",\"points\":[";
    for (unsigned k = 0; k <= t.completed; k++)
    {
        auto &p = t.points[k];
        if (k)
            o << ',';
        o << "{\"step\":" << k << ",\"x\":[";
        for (unsigned j = 0; j < p.state.x.size(); j++)
        {
            if (j)
                o << ',';
            o << vec(p.state.x[j]);
        }
        o << "],\"q\":[";
        for (unsigned j = 0; j < p.state.q.size(); j++)
        {
            if (j)
                o << ',';
            auto q = p.state.q[j];
            o << '[' << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ']';
        }
        o << "],\"v\":[";
        for (unsigned j = 0; j < p.velocity.size(); j++)
        {
            if (j)
                o << ',';
            o << vec(p.velocity[j]);
        }
        o << "],\"y\":[";
        for (unsigned j = 0; j < p.target.size(); j++)
        {
            if (j)
                o << ',';
            o << vec(p.target[j]);
        }
        o << "],\"acceleration\":[";
        for (unsigned j = 0; j < p.acceleration.size(); j++)
        {
            if (j)
                o << ',';
            o << vec(p.acceleration[j]);
        }
        o << "],\"Fbefore\":" << p.before << ",\"Fafter\":" << p.F << ",\"strain\":" << p.strain
          << ",\"SSvectorError\":" << p.ssError << ",\"kineticEnergy\":" << p.kinetic << ",\"maximumSpeed\":" << p.speed
          << ",\"pinError\":" << p.pinError << ",\"positionResidual\":" << p.positionResidual
          << ",\"qResidual\":" << p.qResidual << ",\"acceptedIterations\":" << p.iterations
          << ",\"corrections\":" << p.corrections << ",\"substepCPUms\":" << p.wall
          << ",\"allocationCountingPerformed\":false" << ",\"guessMode\":" << p.guessMode << '}';
    }
    return o.str() + "]}";
}
#include "DynamicsEntry.inl"
