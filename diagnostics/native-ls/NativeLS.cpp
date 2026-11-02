#include "NativeLS.h"
namespace nativels
{
#include "LocalEquations.generated.h"
Workspace::Workspace(const GuideCpuState &c, const windowmath::Settings &s)
    : layout(c, s), n(layout.size), station(n), slot(n)
{
    require(c.ranges.size() == 1, "Single strand diagnostic");
    for (unsigned v = 0; v < c.masses.size(); v++)
    {
        start.push_back(v ? start.back() + size.back() : 0);
        unsigned z = 0;
        for (int k : {layout.x[v], v < layout.q.size() ? layout.q[v] : -1})
            if (k >= 0)
            {
                require(unsigned(k) == start.back() + z, "Station ordering");
                for (unsigned a = 0; a < 3; a++)
                {
                    station[k + a] = v;
                    slot[k + a] = z + a;
                }
                z += 3;
            }
        size.push_back(z);
    }
    auto count = size.size();
    for (auto *a : {&hdiag, &hlo, &mdiag, &mlo, &L, &C})
        a->resize(count);
    for (auto *a : {&gradient, &scale, &rhs, &u, &du, &y, &correction, &delta})
        a->resize(n);
    ext.resize(n);
    trial.x.resize(c.masses.size());
    trial.q.resize(c.lengths.size());
    unsigned rows = 3 * unsigned(c.masses.size()) + 3 * unsigned(c.lengths.size()) + 4 * unsigned(c.bends.size());
    raw.reserve(rows);
    scaled.reserve(rows + n);
}
void Workspace::assemble(const GuideCpuState &c, const windowmath::State &s, const std::vector<D> &targets,
                         const windowmath::Settings &settings)
{
    auto t = Clock::now();
    raw.clear();
    energy = 0;
    emitPhysics(*this, c, s, targets, settings, layout, true);
    timing.rows += ms(t);
    t = Clock::now();
    std::fill(gradient.begin(), gradient.end(), 0);
    std::fill(hdiag.begin(), hdiag.end(), Mat{});
    std::fill(hlo.begin(), hlo.end(), Mat{});
    for (const auto &row : raw)
    {
        unsigned lo = unsigned(size.size()), hi = 0;
        bool any = false;
        for (auto [a, ja] : row.j)
        {
            if (ja != 0)
            {
                lo = std::min(lo, station[a]);
                hi = std::max(hi, station[a]);
                any = true;
            }
            gradient[a] += ja * row.r;
            for (auto [b, jb] : row.j)
            {
                unsigned sa = station[a], sb = station[b];
                double v = ja * jb;
                if (sa == sb)
                    at(hdiag[sa], slot[a], slot[b]) += v;
                else if (sa == sb + 1)
                    at(hlo[sa], slot[a], slot[b]) += v;
                else if (sb != sa + 1)
                    require(v == 0, "Unexpected off-band term");
            }
        }
        if (any)
        {
            maxRowSpan = std::max(maxRowSpan, hi - lo);
            require(hi - lo <= 1, "Nonlocal Jacobian row");
        }
    }
    for (unsigned j = 0; j < n; j++)
        scale[j] = std::sqrt(std::max(h(j, j), 1e-30));
    timing.bands += ms(t);
}
double Workspace::h(unsigned a, unsigned b) const
{
    unsigned sa = station[a], sb = station[b];
    if (sa == sb)
        return at(hdiag[sa], slot[a], slot[b]);
    if (sa == sb + 1)
        return at(hlo[sa], slot[a], slot[b]);
    if (sb == sa + 1)
        return at(hlo[sb], slot[b], slot[a]);
    return 0;
}
double Workspace::m(unsigned a, unsigned b) const
{
    unsigned sa = station[a], sb = station[b];
    if (sa == sb)
        return at(mdiag[sa], slot[a], slot[b]);
    if (sa == sb + 1)
        return at(mlo[sa], slot[a], slot[b]);
    if (sb == sa + 1)
        return at(mlo[sb], slot[b], slot[a]);
    return 0;
}
void Workspace::prepare(double lambda)
{
    auto t = Clock::now();
    scaled = raw;
    for (auto &row : scaled)
    {
        row.r = -row.r;
        for (unsigned k = 0; k < row.j.count; k++)
        {
            auto &[j, a] = row.j.data[k];
            a /= scale[j];
        }
    }
    for (unsigned j = 0; j < n; j++)
    {
        Row r{};
        r.j.emplace_back(j, std::sqrt(lambda));
        scaled.push_back(r);
        rhs[j] = -gradient[j] / scale[j];
    }
    for (unsigned i = 0; i < size.size(); i++)
    {
        unsigned si = start[i], ni = size[i], np = i ? size[i - 1] : 0, sp = i ? start[i - 1] : 0;
        mdiag[i] = {};
        mlo[i] = {};
        for (unsigned a = 0; a < ni; a++)
        {
            for (unsigned b = 0; b < ni; b++)
                at(mdiag[i], a, b) = at(hdiag[i], a, b) / (scale[si + a] * scale[si + b]) + (a == b ? lambda : 0);
            for (unsigned b = 0; b < np; b++)
                at(mlo[i], a, b) = at(hlo[i], a, b) / (scale[si + a] * scale[sp + b]);
        }
    }
    timing.bands += ms(t);
}
bool Workspace::factor()
{
    auto t = Clock::now();
    timing.factors++;
    std::fill(L.begin(), L.end(), Mat{});
    std::fill(C.begin(), C.end(), Mat{});
    for (unsigned i = 0; i < size.size(); i++)
    {
        unsigned ni = size[i], np = i ? size[i - 1] : 0;
        for (unsigned a = 0; a < ni; a++)
            for (unsigned k = 0; k < np; k++)
            {
                double v = at(mlo[i], a, k);
                for (unsigned j = 0; j < k; j++)
                    v -= at(L[i - 1], k, j) * at(C[i], a, j);
                at(C[i], a, k) = v / at(L[i - 1], k, k);
            }
        for (unsigned a = 0; a < ni; a++)
            for (unsigned b = 0; b <= a; b++)
            {
                double v = at(mdiag[i], a, b);
                for (unsigned k = 0; k < np; k++)
                    v -= at(C[i], a, k) * at(C[i], b, k);
                for (unsigned k = 0; k < b; k++)
                    v -= at(L[i], a, k) * at(L[i], b, k);
                if (a == b)
                {
                    if (!(v > 0 && std::isfinite(v)))
                        return false;
                    at(L[i], a, b) = std::sqrt(v);
                }
                else
                    at(L[i], a, b) = v / at(L[i], b, b);
            }
    }
    timing.factor += ms(t);
    return true;
}
void Workspace::triangular(const std::vector<double> &r, std::vector<double> &out)
{
    timing.solves++;
    for (unsigned i = 0; i < size.size(); i++)
    {
        unsigned si = start[i], ni = size[i], np = i ? size[i - 1] : 0, sp = i ? start[i - 1] : 0;
        for (unsigned a = 0; a < ni; a++)
        {
            double v = r[si + a];
            for (unsigned k = 0; k < np; k++)
                v -= at(C[i], a, k) * y[sp + k];
            for (unsigned k = 0; k < a; k++)
                v -= at(L[i], a, k) * y[si + k];
            y[si + a] = v / at(L[i], a, a);
        }
    }
    for (unsigned i = unsigned(size.size()); i-- > 0;)
    {
        unsigned si = start[i], ni = size[i], nn = i + 1 < size.size() ? size[i + 1] : 0,
                 sn = i + 1 < size.size() ? start[i + 1] : 0;
        for (unsigned a = ni; a-- > 0;)
        {
            double v = y[si + a];
            for (unsigned k = 0; k < nn; k++)
                v -= at(C[i + 1], k, a) * out[sn + k];
            for (unsigned k = a + 1; k < ni; k++)
                v -= at(L[i], k, a) * out[si + k];
            out[si + a] = v / at(L[i], a, a);
        }
    }
}
void Workspace::residual(Mode mode)
{
    auto t = Clock::now();
    if (mode == Mode::NativeDouble)
    {
        std::fill(correction.begin(), correction.end(), 0);
        for (auto &row : scaled)
        {
            double rho = row.r;
            for (auto [j, a] : row.j)
                rho -= a * u[j];
            for (auto [j, a] : row.j)
                correction[j] += a * rho;
        }
    }
    else
    {
        std::fill(ext.begin(), ext.end(), dd_real(0));
        for (auto &row : scaled)
        {
            dd_real rho(row.r);
            for (auto [j, a] : row.j)
                rho -= dd_real(a) * dd_real(u[j]);
            for (auto [j, a] : row.j)
                ext[j] += dd_real(a) * rho;
        }
        for (unsigned j = 0; j < n; j++)
            correction[j] = to_double(ext[j]);
    }
    timing.residual += ms(t);
}
void Workspace::update()
{
    for (unsigned j = 0; j < n; j++)
    {
        double old = u[j];
        u[j] += du[j];
        if (u[j] == old && du[j] != 0)
            timing.lost++;
        require(std::isfinite(u[j]), "Nonfinite refinement");
    }
    for (unsigned j = 0; j < n; j++)
        delta[j] = u[j] / scale[j];
}
bool Workspace::solve(unsigned count, Mode mode)
{
    if (!factor())
        return false;
    auto t = Clock::now();
    triangular(rhs, u);
    timing.initial += ms(t);
    for (unsigned k = 0; k < count; k++)
    {
        residual(mode);
        t = Clock::now();
        triangular(correction, du);
        timing.corrections += ms(t);
        timing.refinements++;
        update();
    }
    for (unsigned j = 0; j < n; j++)
        delta[j] = u[j] / scale[j];
    return true;
}
size_t Workspace::bytes() const
{
    size_t b = sizeof(*this);
    b += (raw.capacity() + scaled.capacity()) * sizeof(Row);
    for (auto *v : {&hdiag, &hlo, &mdiag, &mlo, &L, &C})
        b += v->capacity() * sizeof(Mat);
    for (auto *v : {&gradient, &scale, &rhs, &u, &du, &y, &correction, &delta})
        b += v->capacity() * 8;
    b += ext.capacity() * sizeof(dd_real) + (trial.x.capacity() + trial.q.capacity()) * sizeof(D);
    for (auto *v : {&size, &start, &station, &slot})
        b += v->capacity() * 4;
    b += (layout.x.capacity() + layout.q.capacity()) * sizeof(int);
    return b;
}
void applyInto(windowmath::State &result, const windowmath::State &state, const Layout &l,
               const std::vector<double> &step, double scale)
{
    result = state;
    for (unsigned v = 0; v < l.x.size(); v++)
        if (l.x[v] >= 0)
            for (unsigned k = 0; k < 3; k++)
                result.x[v][int(k)] += scale * step[unsigned(l.x[v]) + k];
    for (unsigned s = 0; s < l.q.size(); s++)
        if (l.q[s] >= 0)
        {
            D angle{scale * step[unsigned(l.q[s])], scale * step[unsigned(l.q[s]) + 1],
                    scale * step[unsigned(l.q[s]) + 2]};
            double n = angle.norm();
            D rotation = n > 1e-14 ? D{angle.x * std::sin(n * .5) / n, angle.y * std::sin(n * .5) / n,
                                       angle.z * std::sin(n * .5) / n, std::cos(n * .5)}
                                   : D{angle.x * .5, angle.y * .5, angle.z * .5, 1}.unit();
            result.q[s] = (rotation * state.q[s]).unit();
        }
}
struct EnergyOnly
{
    double energy = 0;
    void row(double r, const SmallRow &)
    {
        energy += .5 * r * r;
    }
};
double objective(const GuideCpuState &c, const windowmath::State &s, const std::vector<D> &y,
                 const windowmath::Settings &settings, const Layout &l)
{
    EnergyOnly e;
    emitPhysics(e, c, s, y, settings, l, false);
    return e.energy;
}
Result minimize(Workspace &w, const GuideCpuState &c, windowmath::State &s, const std::vector<D> &y,
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
            if (!w.solve(count, mode))
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
