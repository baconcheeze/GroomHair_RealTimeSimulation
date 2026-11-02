#pragma once
#include "../../src/AutonomousReference.h"
#include "../../src/OrientationFixtures.h"
#include <array>
#include <chrono>
#include <qd/dd_real.h>
#include <span>
namespace nativels
{
using autonomous::ReferenceResult;
using hair::D;
// Avoid constructing std::string on every successful invariant check.
inline void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
using Clock = std::chrono::steady_clock;
inline double ms(Clock::time_point t)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
struct Layout
{
    std::vector<int> x, q;
    unsigned size = 0;
    Layout(const GuideCpuState &c, const windowmath::Settings &s)
    {
        x.resize(c.masses.size(), -1);
        q.resize(c.lengths.size(), -1);
        for (auto r : c.ranges)
            for (unsigned j = 0; j < r.vertexCount; j++)
            {
                if (!(j == 0 && s.pinX))
                {
                    x[r.firstVertex + j] = size;
                    size += 3;
                }
                if (j < r.segmentCount && !(j == 0 && s.pinQ))
                {
                    q[r.firstSegment + j] = size;
                    size += 3;
                }
            }
    }
};
inline D axis(unsigned i)
{
    D e;
    e[int(i)] = 1;
    return e;
}
struct SmallRow
{
    std::array<std::pair<unsigned, double>, 6> data{};
    unsigned count = 0;
    void emplace_back(unsigned col, double v)
    {
        require(count < 6, "Row support capacity");
        data[count++] = {col, v};
    }
    auto begin() const
    {
        return data.begin();
    }
    auto end() const
    {
        return data.begin() + count;
    }
};
struct Row
{
    double r = 0;
    SmallRow j;
};
using Mat = std::array<double, 36>;
inline double &at(Mat &a, unsigned i, unsigned j)
{
    return a[i * 6 + j];
}
inline double at(const Mat &a, unsigned i, unsigned j)
{
    return a[i * 6 + j];
}
struct Timing
{
    double rows = 0, bands = 0, factor = 0, initial = 0, residual = 0, corrections = 0, trial = 0, nonlinear = 0;
    uint64_t factors = 0, solves = 0, refinements = 0, trials = 0, lost = 0;
};
enum class Mode
{
    NativeDouble,
    NativeExtended
};
struct Workspace
{
    Layout layout;
    unsigned n;
    std::vector<unsigned> size, start, station, slot;
    std::vector<Row> raw, scaled;
    std::vector<Mat> hdiag, hlo, mdiag, mlo, L, C;
    std::vector<double> gradient, scale, rhs, u, du, y, correction, delta;
    std::vector<dd_real> ext;
    windowmath::State trial;
    double energy = 0;
    Timing timing;
    unsigned maxRowSpan = 0;
    Workspace(const GuideCpuState &, const windowmath::Settings &);
    void row(double r, const SmallRow &j)
    {
        raw.push_back({r, j});
        energy += .5 * r * r;
    }
    void assemble(const GuideCpuState &, const windowmath::State &, const std::vector<D> &,
                  const windowmath::Settings &);
    double h(unsigned a, unsigned b) const;
    double m(unsigned a, unsigned b) const;
    void prepare(double lambda);
    bool factor();
    void triangular(const std::vector<double> &, std::vector<double> &);
    void residual(Mode);
    bool solve(unsigned corrections, Mode);
    void update();
    size_t bytes() const;
};
struct Result
{
    ReferenceResult diagnostics;
    unsigned accepted = 0, rejected = 0;
    bool factorFailure = false, noAcceptedTrial = false;
};
void applyInto(windowmath::State &, const windowmath::State &, const Layout &, const std::vector<double> &, double);
double objective(const GuideCpuState &, const windowmath::State &, const std::vector<D> &, const windowmath::Settings &,
                 const Layout &);
Result minimize(Workspace &, const GuideCpuState &, windowmath::State &, const std::vector<D> &,
                const windowmath::Settings &, unsigned corrections = 5, Mode = Mode::NativeExtended,
                double ptol = 1e-11, double qtol = 1e-9);
} // namespace nativels
