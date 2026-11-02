#include "Hybrid.h"
#include "Validation.h"
namespace gpuls
{
double percentile(std::vector<double> a, double fraction)
{
    std::sort(a.begin(), a.end());
    return a[size_t(std::ceil(fraction * a.size())) - 1];
}
int batchBenchmark(Context &c, const fs::path &inputs, const fs::path &out)
{
    std::vector<std::unique_ptr<nativels::Workspace>> refs;
    for (auto name : {"primary__original_initial.json", "heldout-slot0__original_initial.json",
                      "heldout-slot2__original_initial.json"})
    {
        Fixture f(inputs / name);
        auto cpu = dummy(f);
        windowmath::Settings settings;
        settings.pinX = f.mx[0] < 0;
        settings.pinQ = f.mq[0] < 0;
        auto w = std::make_unique<nativels::Workspace>(cpu, settings);
        import(*w, f);
        require(w->solve(5, nativels::Mode::NativeExtended), "CPU batch reference failed");
        refs.push_back(std::move(w));
    }
    std::vector<std::vector<double>> certified(3);
    bool all = true;
    std::ostringstream o;
    o << std::setprecision(17)
      << "{\"distribution\":\"round-robin replicated primary9852/slot0-source14/slot2-source59 frozen initial systems; "
         "NOT 2839 groom strands\",\"tileSystems\":100,\"warmup\":2,\"samples\":7,\"batches\":[";
    bool first = true;
    for (unsigned count : {1u, 10u, 100u, 2839u})
    {
        Pack p(count);
        for (unsigned i = 0; i < count; i++)
            p.system(i, *refs[i % 3]);
        Engine e(c, p.bytes.size(), p.scratchBytes);
        e.prepare(p);
        std::vector<double> factor, initial, corrections, total, wall;
        unsigned checked = 0;
        double maxError = 0;
        bool pass = true;
        for (unsigned sample = 0; sample < 9; sample++)
        {
            auto r = e.run(5);
            if (sample >= 2)
            {
                factor.push_back(r.factor);
                initial.push_back(r.initial);
                corrections.push_back(r.corrections);
                total.push_back(r.total);
                wall.push_back(r.wall);
            }
            for (unsigned i = 0; i < count; i++)
            {
                Status s;
                std::memcpy(&s, r.bytes.data() + p.systems[i].statusOffset, sizeof(s));
                auto &ref = *refs[i % 3];
                std::vector<double> u(ref.n);
                std::memcpy(u.data(), r.bytes.data() + p.systems[i].outputOffset, ref.n * 8);
                double maxDiff = 0;
                for (unsigned j = 0; j < ref.n; j++)
                    maxDiff = std::max(maxDiff, std::abs((u[j] - ref.u[j]) / ref.scale[j]));
                maxError = std::max(maxError, maxDiff);
                if (certified[i % 3].empty())
                {
                    auto savedU = ref.u, savedDelta = ref.delta;
                    ref.u = u;
                    for (unsigned j = 0; j < ref.n; j++)
                        ref.delta[j] = u[j] / ref.scale[j];
                    auto gate = ::error(ref, savedU, savedDelta);
                    ref.u = savedU;
                    ref.delta = savedDelta;
                    require(gate.pass, "Batch unique input strict gate failed");
                    certified[i % 3] = u;
                }
                pass &= std::memcmp(u.data(), certified[i % 3].data(), u.size() * 8) == 0;
                pass &= s.code == 0 && s.done == 1 && s.corrections == 5 && std::isfinite(maxDiff) && maxDiff < 1e-9;
                checked++;
            }
        }
        auto raw = e.run(5, false, false, false);
        bool uninstrumentedPass = true;
        for (unsigned i = 0; i < count; i++)
        {
            Status s;
            std::memcpy(&s, raw.bytes.data() + p.systems[i].statusOffset, sizeof(s));
            uninstrumentedPass &= s.code == 0 && s.done == 1 && s.corrections == 5;
            for (unsigned j = 0; j < refs[i % 3]->n; j++)
            {
                double v;
                std::memcpy(&v, raw.bytes.data() + p.systems[i].outputOffset + j * 8, 8);
                uninstrumentedPass &= std::bit_cast<uint64_t>(v) == std::bit_cast<uint64_t>(certified[i % 3][j]) &&
                                      std::isfinite(v) &&
                                      std::abs((v - refs[i % 3]->u[j]) / refs[i % 3]->scale[j]) < 1e-9;
            }
        }
        all &= pass && uninstrumentedPass;
        if (!first)
            o << ',';
        first = false;
        o << "{\"systems\":" << count
          << ",\"DOFsPerSystem\":183,\"corrections\":5,\"tilesPerStage\":" << (count + 99) / 100
          << ",\"dispatchesPerSolve\":" << ((count + 99) / 100) * 7 << ",\"inputDefaultBytes\":" << p.bytes.size()
          << ",\"scratchDefaultBytes\":" << p.scratchBytes << ",\"persistentUploadBytes\":" << p.bytes.size()
          << ",\"persistentReadbackBytes\":" << p.scratchBytes << ",\"uploadWaitMs\":" << e.uploadWall
          << ",\"checkedResultSlots\":" << checked << ",\"maxPhysicalComponentDifference\":" << maxError
          << ",\"pass\":" << (pass ? "true" : "false")
          << ",\"uninstrumentedPass\":" << (uninstrumentedPass ? "true" : "false")
          << ",\"uninstrumentedHostWallMs\":" << raw.wall << ",\"GPUms\":{";
        const char *names[] = {"factor", "initial", "corrections", "total"};
        std::vector<double> *values[] = {&factor, &initial, &corrections, &total};
        for (unsigned k = 0; k < 4; k++)
        {
            if (k)
                o << ',';
            o << '"' << names[k] << "\":{\"median\":" << percentile(*values[k], .5)
              << ",\"P95\":" << percentile(*values[k], .95) << '}';
        }
        o << "},\"hostSolveReadbackWaitMs\":{\"median\":" << percentile(wall, .5)
          << ",\"P95\":" << percentile(wall, .95) << "}}";
        std::cout << "batch " << count << " gpuMedian=" << percentile(total, .5) << "ms pass=" << pass << '\n';
    }
    o << "]}";
    text(out / "benchmark.json", o.str());
    c.audit();
    return all ? 0 : 1;
}
} // namespace gpuls
