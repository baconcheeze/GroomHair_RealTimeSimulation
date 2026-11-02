#include "WindowMath.h"
#include "OrientationFixtures.h"
#include <sstream>
namespace windowmath
{
using hair::require;
D gradient(const PositionProblem &p)
{
    return p.a * (p.x - p.y) + p.cl * (p.x - p.left - p.tl) + p.cr * (p.x - p.right + p.tr);
}
D positionMinimum(const PositionProblem &p)
{
    double d = p.a + p.cl + p.cr;
    require(p.a > 0 && p.cl >= 0 && p.cr >= 0 && std::isfinite(d) && d > 0, "Invalid position coefficients");
    return p.pinned ? p.x : p.x - gradient(p) / d;
}
double localEnergy(const PositionProblem &p, D x)
{
    auto i = x - p.y, l = x - p.left - p.tl, r = x - p.right + p.tr;
    return .5 * (p.a * i.dot(i) + p.cl * l.dot(l) + p.cr * r.dot(r));
}
std::vector<D> tridiagonal(const std::vector<D> &y, const std::vector<double> &a, const std::vector<double> &c,
                           const std::vector<D> &t, bool pin, D root)
{
    size_t n = y.size();
    require(n >= 2 && a.size() == n && c.size() + 1 == n && t.size() == c.size(), "Tridiagonal shape");
    std::vector<double> diagonal = a, lower(n), upper(n);
    std::vector<D> rhs(n);
    for (size_t v = 0; v < n; ++v)
    {
        rhs[v] = a[v] * y[v];
        if (v)
        {
            diagonal[v] += c[v - 1];
            lower[v] = -c[v - 1];
            rhs[v] = rhs[v] + c[v - 1] * t[v - 1];
        }
        if (v + 1 < n)
        {
            diagonal[v] += c[v];
            upper[v] = -c[v];
            rhs[v] = rhs[v] - c[v] * t[v];
        }
    }
    if (pin)
    {
        rhs[1] = rhs[1] - lower[1] * root;
        lower[1] = 0;
        diagonal[0] = 1;
        upper[0] = 0;
        rhs[0] = root;
    }
    for (size_t v = 1; v < n; ++v)
    {
        double multiplier = lower[v] / diagonal[v - 1];
        diagonal[v] -= multiplier * upper[v - 1];
        rhs[v] = rhs[v] - multiplier * rhs[v - 1];
    }
    std::vector<D> x(n);
    x[n - 1] = rhs[n - 1] / diagonal[n - 1];
    for (size_t v = n - 1; v-- > 0;)
        x[v] = (rhs[v] - upper[v] * x[v + 1]) / diagonal[v];
    return x;
}
State rest(const GuideCpuState &cpu)
{
    State s;
    for (auto p : cpu.source->positions)
        s.x.emplace_back(p.x, p.y, p.z);
    for (auto q : cpu.orientations)
        s.q.emplace_back(q);
    return s;
}
std::vector<Float3> targets(const GuideCpuState &cpu, const std::string &mode)
{
    auto y = cpu.source->positions;
    require(mode == "rest" || mode == "displaced", "Unknown target mode");
    if (mode == "rest")
        return y;
    for (auto r : cpu.ranges)
        for (unsigned j = 1; j < r.vertexCount; ++j)
        {
            float u = float(j) / r.segmentCount, s = std::sin(1.57079632679f * u);
            auto &p = y[r.firstVertex + j];
            p.x += .012f * s * s;
            p.y -= .005f * u * u;
            p.z += .006f * std::sin(3.14159265359f * u);
        }
    return y;
}
namespace
{
D vector(Float3 p)
{
    return {p.x, p.y, p.z};
}
PositionProblem problem(const GuideCpuState &cpu, const State &state, const std::vector<Float3> &y, const Settings &set,
                        GuideRange r, unsigned j)
{
    unsigned v = r.firstVertex + j;
    PositionProblem p;
    p.x = state.x[v];
    p.y = vector(y[v]);
    p.a = cpu.masses[v] / (double(set.h) * set.h);
    p.pinned = j == 0 && set.pinX;
    if (j)
    {
        unsigned s = r.firstSegment + j - 1;
        double l = cpu.lengths[s];
        p.cl = set.ss / l;
        p.left = state.x[v - 1];
        p.tl = l * hair::rotate(state.q[s], D{0, 0, 1});
    }
    if (j < r.segmentCount)
    {
        unsigned s = r.firstSegment + j;
        double l = cpu.lengths[s];
        p.cr = set.ss / l;
        p.right = state.x[v + 1];
        p.tr = l * hair::rotate(state.q[s], D{0, 0, 1});
    }
    return p;
}
void coefficients(const GuideCpuState &cpu, const State &state, const Settings &set, GuideRange r, unsigned j, D &v,
                  D &b)
{
    unsigned s = r.firstSegment + j, x = r.firstVertex + j;
    v = (-2 * double(set.ss)) * (state.x[x + 1] - state.x[x]);
    D q = state.q[s];
    b = {};
    if (j)
    {
        D target(cpu.bends[r.firstBend + j - 1]), left = state.q[s - 1];
        b = b + (4 * double(set.bt) / cpu.lengths[s - 1] * ((left.conj() * q).dot(target) >= 0 ? 1. : -1.)) *
                    (left * target);
    }
    if (j + 1 < r.segmentCount)
    {
        D target(cpu.bends[r.firstBend + j]), right = state.q[s + 1];
        b = b + (4 * double(set.bt) / cpu.lengths[s] * ((q.conj() * right).dot(target) >= 0 ? 1. : -1.)) *
                    (right * target.conj());
    }
}
} // namespace
void uncachedSweep(const GuideCpuState &cpu, State &state, const std::vector<Float3> &y, const Settings &set,
                   unsigned limit, bool fixedQ)
{
    unsigned count =
        set.participating ? std::min(set.participating, unsigned(cpu.ranges.size())) : unsigned(cpu.ranges.size());
    for (unsigned strand = 0; strand < count; ++strand)
    {
        auto r = cpu.ranges[strand];
        unsigned ops = 0;
        bool stopped = false;
        auto p = [&](unsigned j) {
            if (stopped)
                return;
            auto local = problem(cpu, state, y, set, r, j);
            state.x[r.firstVertex + j] = positionMinimum(local);
            if (limit && ++ops >= limit)
                stopped = true;
        };
        auto q = [&](unsigned j) {
            if (stopped)
                return;
            if (!fixedQ && !(j == 0 && set.pinQ))
            {
                D v, b;
                coefficients(cpu, state, set, r, j, v, b);
                auto s = r.firstSegment + j;
                D next = hair::localReference(v, b, state.q[s]);
                if (next.dot(state.q[s]) < 0)
                    next = -next;
                state.q[s] = next;
            }
            if (limit && ++ops >= limit)
                stopped = true;
        };
        p(0);
        if (r.segmentCount == 1)
        {
            p(1);
            q(0);
            p(1);
        }
        else
        {
            for (unsigned i = 0; i + 1 < r.segmentCount && !stopped; ++i)
            {
                p(i + 1);
                q(i + 1);
                q(i);
                p(i + 1);
            }
            p(r.segmentCount);
            q(r.segmentCount - 1);
            p(r.segmentCount);
        }
    }
}
Energy measure(const GuideCpuState &cpu, const State &state, const std::vector<Float3> &y, const Settings &set)
{
    Energy e;
    unsigned count =
        set.participating ? std::min(set.participating, unsigned(cpu.ranges.size())) : unsigned(cpu.ranges.size());
    for (unsigned strand = 0; strand < count; ++strand)
    {
        auto r = cpu.ranges[strand];
        for (unsigned j = 0; j < r.vertexCount; ++j)
        {
            unsigned v = r.firstVertex + j;
            auto p = problem(cpu, state, y, set, r, j);
            D d = state.x[v] - vector(y[v]);
            e.inertia += .5 * p.a * d.dot(d);
            if (!(j == 0 && set.pinX))
            {
                double g = gradient(p).norm();
                e.gradient = std::max(e.gradient, g);
                e.correction = std::max(e.correction, g / (p.a + p.cl + p.cr));
            }
        }
        for (unsigned j = 0; j < r.segmentCount; ++j)
        {
            unsigned s = r.firstSegment + j, v = r.firstVertex + j;
            double l = cpu.lengths[s];
            D q = state.q[s], css = (state.x[v + 1] - state.x[v]) / l - hair::rotate(q, D{0, 0, 1});
            e.ss += .5 * set.ss * l * css.dot(css);
            e.norm = std::max(e.norm, std::abs(q.norm() - 1));
            if (j + 1 < r.segmentCount)
            {
                D target(cpu.bends[r.firstBend + j]), rel = q.conj() * state.q[s + 1],
                                                      c = rel - (rel.dot(target) >= 0 ? 1. : -1.) * target;
                e.bt += .5 * (4 * set.bt / l) * c.dot(c);
            }
            if (!(j == 0 && set.pinQ))
            {
                D a, b;
                coefficients(cpu, state, set, r, j, a, b);
                double spectral, scale = a.norm() + b.norm(), res = hair::residual(a, b, q, spectral);
                if (scale)
                {
                    e.stationarity = std::max(e.stationarity, res / scale);
                    e.spectral = std::max(e.spectral, spectral / scale);
                }
                if (j && std::abs((state.q[s - 1].conj() * q).dot(D(cpu.bends[r.firstBend + j - 1]))) <= 1e-6)
                    ++e.ties;
                if (j + 1 < r.segmentCount &&
                    std::abs((q.conj() * state.q[s + 1]).dot(D(cpu.bends[r.firstBend + j]))) <= 1e-6)
                    ++e.ties;
            }
        }
    }
    return e;
}
std::string Energy::json() const
{
    std::ostringstream s;
    s.precision(15);
    s << "{\"InertialEnergy\":" << inertia << ",\"ShearStretchEnergy\":" << ss << ",\"BendTwistEnergy\":" << bt
      << ",\"TotalObjective\":" << total() << ",\"MaxPositionGradient\":" << gradient
      << ",\"MaxPositionCorrectionMeters\":" << correction << ",\"NormalizedQuaternionStationarity\":" << stationarity
      << ",\"NormalizedSpectralViolation\":" << spectral << ",\"QuaternionNormError\":" << norm
      << ",\"PoleTies\":" << ties << "}";
    return s.str();
}
std::string validatePositionMath()
{
    double maxGradient = 0, maxMinimum = 0, maxEquivalent = 0, maxTri = 0;
    hair::CsRandom random(31871);
    for (unsigned i = 0; i < 256; ++i)
    {
        auto vec = [&]() { return D{random.sample() - .5, random.sample() - .5, random.sample() - .5}; };
        PositionProblem p;
        p.x = vec();
        p.y = vec();
        p.left = vec();
        p.right = vec();
        p.tl = vec();
        p.tr = vec();
        p.a = .1 + random.sample() * 9;
        p.cl = i % 3 == 0 ? 0 : .2 + random.sample() * 13;
        p.cr = i % 3 == 1 ? 0 : .3 + random.sample() * 17;
        auto solution = positionMinimum(p), g = gradient(p);
        double diagonal = p.a + p.cl + p.cr;
        D numerator = (p.a * p.y + p.cl * (p.left + p.tl) + p.cr * (p.right - p.tr)) / diagonal;
        maxEquivalent = std::max(maxEquivalent, (solution - numerator).norm());
        auto at = p;
        at.x = solution;
        maxMinimum = std::max(maxMinimum, gradient(at).norm());
        require(localEnergy(p, solution) <= localEnergy(p, p.x) + 1e-12, "Position local energy increased");
        for (int axis = 0; axis < 3; ++axis)
        {
            D d;
            d[axis] = 1;
            double step = 1e-5, fd = (localEnergy(p, p.x + step * d) - localEnergy(p, p.x - step * d)) / (2 * step);
            maxGradient = std::max(maxGradient, std::abs(fd - g[axis]));
        }
        p.pinned = true;
        require((positionMinimum(p) - p.x).norm() == 0, "Position pin");
    }
    for (bool pinned : {false, true})
    {
        std::vector<D> y{{.1, .2, 0}, {.2, .1, 0}, {.4, .3, .1}, {.6, .1, .2}},
            t{{.1, 0, 0}, {.15, .05, 0}, {.2, 0, .1}};
        std::vector<double> a{2, 7, 3, 11}, c{4, 13, 6};
        auto expected = tridiagonal(y, a, c, t, pinned, {0, 0, 0});
        auto actual = y;
        if (pinned)
            actual[0] = {};
        for (unsigned it = 0; it < 512; ++it)
            for (unsigned v = 0; v < 4; ++v)
            {
                PositionProblem p;
                p.x = actual[v];
                p.y = y[v];
                p.a = a[v];
                p.pinned = v == 0 && pinned;
                if (v)
                {
                    p.left = actual[v - 1];
                    p.cl = c[v - 1];
                    p.tl = t[v - 1];
                }
                if (v + 1 < 4)
                {
                    p.right = actual[v + 1];
                    p.cr = c[v];
                    p.tr = t[v];
                }
                actual[v] = positionMinimum(p);
            }
        for (unsigned v = 0; v < 4; ++v)
            maxTri = std::max(maxTri, (actual[v] - expected[v]).norm());
    }
    require(maxGradient < 1e-8 && maxMinimum < 1e-12 && maxEquivalent < 1e-12 && maxTri < 1e-12,
            "Position double oracle tolerance");
    std::ostringstream s;
    s.precision(17);
    s << "{\"CPUOnly\":true,\"LocalCases\":256,\"FiniteDifferenceMax\":" << maxGradient
      << ",\"LocalGradientAtMinimum\":" << maxMinimum << ",\"NumeratorResidualDifference\":" << maxEquivalent
      << ",\"ModerateTridiagonalError\":" << maxTri << "}";
    return s.str();
}
} // namespace windowmath
