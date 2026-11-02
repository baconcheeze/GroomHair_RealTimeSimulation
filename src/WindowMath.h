#pragma once
#include "GuideState.h"
namespace windowmath
{
using hair::D;
struct PositionProblem
{
    D x, y, left, right, tl, tr;
    double a{}, cl{}, cr{};
    bool pinned = false;
};
D gradient(const PositionProblem &p);
D positionMinimum(const PositionProblem &p);
double localEnergy(const PositionProblem &p, D x);
std::vector<D> tridiagonal(const std::vector<D> &y, const std::vector<double> &a, const std::vector<double> &c,
                           const std::vector<D> &targetEdges, bool pinRoot, D root);
struct State
{
    std::vector<D> x, q;
};
struct Settings
{
    unsigned participating = 1;
    float h = 1.f / 120, ss = 1, bt = 1e-5f;
    bool pinX = true, pinQ = true;
};
struct Energy
{
    double inertia{}, ss{}, bt{}, gradient{}, correction{}, stationarity{}, spectral{}, norm{};
    unsigned ties{};
    double total() const
    {
        return inertia + ss + bt;
    }
    std::string json() const;
};
State rest(const GuideCpuState &cpu);
std::vector<Float3> targets(const GuideCpuState &cpu, const std::string &mode);
Energy measure(const GuideCpuState &cpu, const State &state, const std::vector<Float3> &y, const Settings &settings);
// Immediate ordinary array writes; intentionally no rolling cache.
void uncachedSweep(const GuideCpuState &cpu, State &state, const std::vector<Float3> &y, const Settings &settings,
                   unsigned operationLimit = 0, bool fixedQ = false);
std::string validatePositionMath();
} // namespace windowmath
