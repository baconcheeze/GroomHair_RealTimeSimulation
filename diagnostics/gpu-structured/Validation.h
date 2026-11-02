#pragma once
#include "../native-ls/Fixture.h"
using hair::D;
struct Error
{
    double pos = 0, angle = 0, relative = 0, retracted = 0, stationarity = 0;
    bool pass = false;
};
inline Error error(nativels::Workspace &w, const std::vector<double> &qrU, const std::vector<double> &qrD)
{
    Error e;
    double delta = 0, mag = 0, pmag = 0, qmag = 0;
    for (unsigned j = 0; j < w.n; j++)
    {
        delta = std::max(delta, std::abs(w.u[j] - qrU[j]));
        mag = std::max(mag, std::abs(qrU[j]));
    }
    e.relative = delta / std::max(mag, 1e-300);
    for (unsigned kind = 0; kind < 2; kind++)
        for (int k : kind ? w.layout.q : w.layout.x)
            if (k >= 0)
            {
                D d{w.delta[k] - qrD[k], w.delta[k + 1] - qrD[k + 1], w.delta[k + 2] - qrD[k + 2]},
                    r{qrD[k], qrD[k + 1], qrD[k + 2]};
                if (kind)
                {
                    e.angle = std::max(e.angle, d.norm());
                    qmag = std::max(qmag, r.norm());
                }
                else
                {
                    e.pos = std::max(e.pos, d.norm());
                    pmag = std::max(pmag, r.norm());
                }
            }
    std::vector<dd_real> s(w.n);
    std::vector<double> cols(w.n);
    double an = 0, bn = 0, un = 0;
    for (double v : w.u)
        un = std::max(un, std::abs(v));
    for (auto &row : w.scaled)
    {
        dd_real rho(row.r);
        double rn = 0;
        for (auto [j, a] : row.j)
        {
            rho -= dd_real(a) * dd_real(w.u[j]);
            rn += std::abs(a);
            cols[j] += std::abs(a);
        }
        an = std::max(an, rn);
        bn = std::max(bn, std::abs(row.r));
        for (auto [j, a] : row.j)
            s[j] += dd_real(a) * rho;
    }
    double sn = 0;
    for (auto v : s)
        sn = std::max(sn, std::abs(to_double(v)));
    double at = *std::max_element(cols.begin(), cols.end());
    e.stationarity = sn / std::max(1e-300, at * (an * un + bn));
    e.pass = (e.relative <= 1e-7 || (e.pos <= 1e-10 && e.angle <= 1e-9)) && e.pos <= std::max(1e-10, 1e-8 * pmag) &&
             e.angle <= std::max(1e-9, 1e-8 * qmag) && e.stationarity <= 5e-14;
    return e;
}
inline GuideCpuState dummy(const Fixture &f)
{
    GuideCpuState c;
    c.masses.resize(f.mx.size());
    c.lengths.resize(f.mq.size());
    c.bends.resize(f.mq.size() - 1);
    c.ranges = {{0, unsigned(f.mx.size()), 0, unsigned(f.mq.size()), 0, unsigned(f.mq.size() - 1)}};
    return c;
}
inline void import(nativels::Workspace &w, const Fixture &f)
{
    auto a = f.bits("A"), b = f.bits("b"), m = f.bits("M");
    w.scale = f.bits("D");
    w.rhs = f.bits("c");
    require(w.n == f.n && a.size() == size_t(f.rows + w.n) * w.n, "Dimensions");
    w.scaled.clear();
    for (unsigned r = 0; r < b.size(); r++)
    {
        nativels::Row row;
        row.r = b[r];
        unsigned lo = unsigned(w.size.size()), hi = 0;
        for (unsigned j = 0; j < w.n; j++)
            if (a[size_t(r) * w.n + j] != 0)
            {
                row.j.emplace_back(j, a[size_t(r) * w.n + j]);
                lo = std::min(lo, w.station[j]);
                hi = std::max(hi, w.station[j]);
            }
        require(row.j.count == 0 || hi - lo <= 1, "Dense-to-local nonlocal row");
        w.scaled.push_back(row);
    }
    require(w.scaled.size() == b.size(), "Rows count");
    for (unsigned j = 0; j < w.n; j++)
        for (unsigned k = 0; k < w.n; k++)
        {
            double v = m[size_t(j) * w.n + k];
            unsigned sj = w.station[j], sk = w.station[k];
            if (sj == sk)
                nativels::at(w.mdiag[sj], w.slot[j], w.slot[k]) = v;
            else if (sj == sk + 1)
                nativels::at(w.mlo[sj], w.slot[j], w.slot[k]) = v;
            else if (sk != sj + 1)
                require(v == 0, "Unexpected M offband");
        }
    for (unsigned j = 0; j < w.n; j++)
        for (unsigned k = 0; k < w.n; k++)
            require(w.m(j, k) == m[size_t(j) * w.n + k], "Band reconstruction");
    for (unsigned r = 0; r < b.size(); r++)
        for (unsigned j = 0; j < w.n; j++)
        {
            double v = 0;
            for (auto [k, x] : w.scaled[r].j)
                if (k == j)
                    v = x;
            require(v == a[size_t(r) * w.n + j], "Row reconstruction");
        }
}
