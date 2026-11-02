#include "../strand-block/Verify.h"
#include "NativeLS.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace fs = std::filesystem;
using hair::D;
using hair::require;
using State = windowmath::State;
#include "Fixture.h"
std::string vec(D v)
{
    std::ostringstream o;
    o << std::setprecision(17) << '[' << v.x << ',' << v.y << ',' << v.z << ']';
    return o.str();
}
std::string array(const std::vector<double> &v)
{
    std::ostringstream o;
    o << std::setprecision(17) << '[';
    bool first = true;
    for (auto x : v)
    {
        if (!first)
            o << ',';
        first = false;
        o << x;
    }
    return o.str() + ']';
}
void write(const fs::path &p, const std::string &s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p);
    f << s;
    require(bool(f), "Output failed");
}
std::string timings(const nativels::Timing &t)
{
    std::ostringstream o;
    o << std::setprecision(17) << "{\"rows\":" << t.rows << ",\"bands\":" << t.bands << ",\"factor\":" << t.factor
      << ",\"initial\":" << t.initial << ",\"residual\":" << t.residual << ",\"corrections\":" << t.corrections
      << ",\"trial\":" << t.trial << ",\"nonlinear\":" << t.nonlinear << ",\"factorCount\":" << t.factors
      << ",\"solveCount\":" << t.solves << ",\"correctionCount\":" << t.refinements << ",\"lostUpdates\":" << t.lost
      << '}';
    return o.str();
}
struct Error
{
    double pos = 0, angle = 0, relative = 0, retracted = 0, stationarity = 0;
    bool pass = false;
};
Error error(nativels::Workspace &w, const std::vector<double> &qrU, const std::vector<double> &qrD)
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
GuideCpuState dummy(const Fixture &f)
{
    GuideCpuState c;
    c.masses.resize(f.mx.size());
    c.lengths.resize(f.mq.size());
    c.bends.resize(f.mq.size() - 1);
    c.ranges = {{0, unsigned(f.mx.size()), 0, unsigned(f.mq.size()), 0, unsigned(f.mq.size() - 1)}};
    return c;
}
void import(nativels::Workspace &w, const Fixture &f)
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
int replay(const fs::path &inputs, const fs::path &out)
{
    unsigned count = 0, passed = 0, control = 0;
    for (auto file : fs::directory_iterator(inputs))
    {
        if (file.path().extension() != ".json")
            continue;
        Fixture f(file.path());
        auto c = dummy(f);
        windowmath::Settings settings;
        settings.pinX = f.mx[0] < 0;
        settings.pinQ = f.mq[0] < 0;
        nativels::Workspace w(c, settings);
        import(w, f);
        auto qu = f.bits("QRU"), qd = f.bits("QRDelta"), initial = f.bits("blockU");
        std::ostringstream o;
        o << std::setprecision(17) << "{\"workspaceBytes\":" << w.bytes() << ",\"n\":" << w.n << ",\"modes\":[";
        bool firstmode = true;
        for (auto mode : {nativels::Mode::NativeDouble, nativels::Mode::NativeExtended})
        {
            if (!firstmode)
                o << ',';
            firstmode = false;
            require(w.factor(), "Frozen factor failure");
            w.triangular(w.rhs, w.u);
            require(w.u == initial, "Initial solve not bitwise reproduced");
            for (unsigned j = 0; j < w.n; j++)
                w.delta[j] = w.u[j] / w.scale[j];
            o << "{\"name\":\"" << (mode == nativels::Mode::NativeDouble ? "NativeDouble" : "NativeExtended")
              << "\",\"curve\":[";
            bool final = false;
            for (unsigned k = 0; k <= 5; k++)
            {
                auto e = error(w, qu, qd);
                if (k)
                    o << ',';
                o << "{\"corrections\":" << k << ",\"pass\":" << (e.pass ? "true" : "false")
                  << ",\"positionError\":" << e.pos << ",\"rawAngleError\":" << e.angle
                  << ",\"scaledRelative\":" << e.relative << ",\"stationarity\":" << e.stationarity
                  << ",\"u\":" << array(w.u) << ",\"delta\":" << array(w.delta) << '}';
                final = e.pass;
                if (k < 5)
                {
                    w.residual(mode);
                    w.triangular(w.correction, w.du);
                    w.update();
                }
            }
            o << "]}";
            if (mode == nativels::Mode::NativeExtended)
                passed += final;
            else
                control += final;
        }
        o << "]}";
        write(out / file.path().filename(), o.str());
        count++;
    }
    std::ostringstream o;
    o << "{\"cases\":" << count << ",\"extendedPass\":" << passed << ",\"doublePass\":" << control << '}';
    write(out / "STATUS.json", o.str());
    std::cout << o.str() << '\n';
    return count && passed == count ? 0 : 1;
}
std::pair<double, double> validateLocal(nativels::Workspace &w, const jointcpu::Problem &p)
{
    w.assemble(p.cpu, p.initial, p.dy, p.set);
    autonomous::Layout l(p.cpu, p.set);
    auto dense = autonomous::evaluate(p.cpu, p.initial, p.dy, p.set, l, true);
    require(w.energy == dense.energy, "Energy mismatch");
    for (unsigned j = 0; j < w.n; j++)
    {
        require(w.gradient[j] == dense.gradient[j], "Gradient mismatch");
        for (unsigned k = 0; k < w.n; k++)
            require(w.h(j, k) == dense.hessian[size_t(j) * w.n + k], "Direct band mismatch");
    }
    strandblock::Rows rows(w.n);
    strandblock::emitPhysics(rows, p.cpu, p.initial, p.dy, p.set, l, true);
    w.prepare(1e-8);
    for (unsigned r = 0; r < rows.r.size(); r++)
    {
        require(w.scaled[r].r == -rows.r[r], "Local RHS mismatch");
        for (unsigned j = 0; j < w.n; j++)
        {
            double value = 0;
            for (auto [k, a] : w.scaled[r].j)
                if (k == j)
                    value = a;
            require(value == rows.j[r][j] / w.scale[j], "Local scaled row mismatch");
        }
    }
    std::vector<double> probe(w.n);
    for (unsigned j = 0; j < w.n; j++)
        probe[j] = .001 * std::sin(.37 * (j + 1));
    std::vector<dd_real> local(w.n), denseAtr(w.n);
    double ax = 0, atr = 0;
    for (unsigned r = 0; r < w.scaled.size(); r++)
    {
        dd_real a(w.scaled[r].r), b(w.scaled[r].r);
        for (auto [j, v] : w.scaled[r].j)
            a -= dd_real(v) * dd_real(probe[j]);
        for (unsigned j = 0; j < w.n; j++)
        {
            double v = r < rows.r.size() ? rows.j[r][j] / w.scale[j] : (j == r - rows.r.size() ? std::sqrt(1e-8) : 0);
            b -= dd_real(v) * dd_real(probe[j]);
        }
        ax = std::max(ax, std::abs(to_double(a - b)));
        for (auto [j, v] : w.scaled[r].j)
            local[j] += dd_real(v) * a;
        for (unsigned j = 0; j < w.n; j++)
        {
            double v = r < rows.r.size() ? rows.j[r][j] / w.scale[j] : (j == r - rows.r.size() ? std::sqrt(1e-8) : 0);
            denseAtr[j] += dd_real(v) * b;
        }
    }
    for (unsigned j = 0; j < w.n; j++)
        atr = std::max(atr, std::abs(to_double(local[j] - denseAtr[j])));
    require(ax < 1e-28 && atr < 1e-28, "Local matvec validation");
    return {ax, atr};
}
int nonlinear(const fs::path &inputs, const fs::path &out)
{
    unsigned count = 0, passed = 0;
    for (auto file : fs::directory_iterator(inputs))
    {
        if (file.path().extension() != ".jwp")
            continue;
        auto p = jointcpu::loadProblem(file.path());
        auto name = file.path().stem().string();
        nativels::Workspace w(p->cpu, p->set);
        auto validation = validateLocal(w, *p);
        auto s = p->initial;
        bool qualified = true;
        unsigned accepted = 0, rejected = 0;
        std::vector<std::pair<double, double>> phases =
            name == "primary" ? std::vector<std::pair<double, double>>{{1e-11, 1e-9}, {1e-12, 1e-10}, {1e-14, 1e-13}}
                              : std::vector<std::pair<double, double>>{{1e-11, 1e-9}, {1e-13, 1e-11}};
        nativels::Result result;
        for (auto [pt, qt] : phases)
        {
            result = nativels::minimize(w, p->cpu, s, p->dy, p->set, 5, nativels::Mode::NativeExtended, pt, qt);
            qualified &= result.diagnostics.qualified;
            accepted += result.accepted;
            rejected += result.rejected;
            if (!qualified)
                break;
        }
        std::ostringstream o;
        o << std::setprecision(17) << "{\"qualified\":" << (qualified ? "true" : "false")
          << ",\"accepted\":" << accepted << ",\"rejected\":" << rejected << ",\"F\":" << result.diagnostics.objective
          << ",\"positionResidual\":" << result.diagnostics.positionCorrection
          << ",\"qResidual\":" << result.diagnostics.stationarity << ",\"spectral\":" << result.diagnostics.spectral
          << ",\"directLocalAuDifference\":" << validation.first
          << ",\"directLocalAtrDifference\":" << validation.second
          << ",\"localRowsBandsBitExact\":true,\"maxRowStationSpan\":" << w.maxRowSpan
          << ",\"workspaceBytes\":" << w.bytes() << ",\"timing\":" << timings(w.timing) << '}';
        write(out / name / "result.json", o.str());
        jointcpu::writeState(out / name / "final.json", s);
        count++;
        passed += qualified;
        std::cout << name << " qualified=" << qualified << " accepted=" << accepted << '\n';
    }
    write(out / "STATUS.json", "{\"cases\":" + std::to_string(count) + ",\"passed\":" + std::to_string(passed) + "}");
    return count == 12 && passed == 12 ? 0 : 1;
}
int dynamics(const fs::path &, const fs::path &);
int benchmark(const fs::path &, const fs::path &);
int main(int argc, char **argv)
{
    try
    {
        require(argc == 4, "NativeLSCandidate replay|nonlinear|dynamics|benchmark inputs output");
        dd_real p = dd_real(1) + dd_real(std::ldexp(1., -105));
        require(to_double(p - dd_real(1)) == std::ldexp(1., -105), "Extended precision probe failed");
        std::string mode = argv[1];
        if (mode == "precision")
        {
            std::ostringstream o;
            o << std::setprecision(17) << "{\"dependency\":\"QD 2.3.24\",\"typeBytes\":" << sizeof(dd_real)
              << ",\"upstreamConservativeDigits\":" << std::numeric_limits<dd_real>::digits
              << ",\"binary64Digits\":" << std::numeric_limits<double>::digits
              << ",\"observedLowBit\":" << to_double(p - dd_real(1)) << ",\"expectedLowBit\":" << std::ldexp(1., -105)
              << ",\"probePass\":true}";
            write(fs::path(argv[3]) / "precision.json", o.str());
            return 0;
        }
        if (mode == "replay")
            return replay(argv[2], argv[3]);
        if (mode == "nonlinear")
            return nonlinear(argv[2], argv[3]);
        if (mode == "dynamics")
            return dynamics(argv[2], argv[3]);
        if (mode == "benchmark")
            return benchmark(argv[2], argv[3]);
        throw std::runtime_error("Unknown mode");
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL " << e.what() << '\n';
        return 2;
    }
}
