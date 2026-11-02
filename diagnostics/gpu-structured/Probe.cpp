#include "Context.h"
#include "Validation.h"
#include <bit>
int main(int argc, char **argv)
{
    try
    {
        hair::require(argc == 3 || argc == 4, "Probe inputs output [unsafe]");
        fs::path input = argv[1], out = argv[2];
        gpuls::Context c;
        gpuls::text(out / "device.json", c.json());
        if (!c.fp64)
        {
            gpuls::text(out / "STATUS.json", "{\"unsupportedFP64\":true}");
            return 3;
        }
        hair::require(unsigned(c.model) >= 0x60, "SM6 unsupported");
        bool unsafe = argc == 4;
        auto root = gpuls::root(c);
        auto ps =
            gpuls::pipeline(c, root.Get(), gpuls::exeDir() / "shaders" / (unsafe ? "UnsafeProbe.dxil" : "Probe.dxil"));
        std::vector<double> v;
        double minPivot = DBL_MAX, maxPivot = 0;
        for (auto file : fs::directory_iterator(input))
        {
            if (file.path().extension() != ".json")
                continue;
            Fixture f(file.path());
            auto cpu = dummy(f);
            windowmath::Settings settings;
            settings.pinX = f.mx[0] < 0;
            settings.pinQ = f.mq[0] < 0;
            nativels::Workspace w(cpu, settings);
            import(w, f);
            hair::require(w.factor(), "CPU probe factorization failed");
            for (unsigned i = 0; i < w.size.size(); i++)
            {
                unsigned np = i ? w.size[i - 1] : 0;
                for (unsigned a = 0; a < w.size[i]; a++)
                {
                    double pivot = nativels::at(w.mdiag[i], a, a);
                    for (unsigned k = 0; k < np; k++)
                        pivot -= nativels::at(w.C[i], a, k) * nativels::at(w.C[i], a, k);
                    for (unsigned k = 0; k < a; k++)
                        pivot -= nativels::at(w.L[i], a, k) * nativels::at(w.L[i], a, k);
                    v.insert(v.end(), {pivot, 3.25});
                    minPivot = std::min(minPivot, pivot);
                    maxPivot = std::max(maxPivot, pivot);
                }
            }
        }
        for (double x : {std::numeric_limits<double>::denorm_min(), DBL_MIN, 1e-300, 1e-100, .25, 1., 2., 4., 1e100,
                         1e300, DBL_MAX})
            v.insert(v.end(), {x, 1.0});
        for (uint64_t bits : {0x3ff0000000000001ull, 0x7ff0000000000000ull, 0xfff0000000000000ull,
                              0x7ff8123456789abcull, 0x8000000000000000ull, 0xbff0000000000000ull})
            v.insert(v.end(), {std::bit_cast<double>(bits), 3.0});
        v.insert(v.end(), {.1, .3, 1.0, 3.0, 1e-20, 1e20});
        unsigned count = unsigned(v.size() / 2);
        guidegpu::Buffer a, b;
        UploadList keep;
        Microsoft::WRL::ComPtr<ID3D12Resource> rb;
        c.submit([&](auto *l) {
            a.create(c.device.Get(), v.size() * 8, false, L"FP64ProbeInput");
            a.upload(c.device.Get(), l, v.data(), v.size() * 8, keep, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            b.create(c.device.Get(), count * 64, true, L"FP64ProbeOutput");
            b.upload(c.device.Get(), l, nullptr, 0, keep, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            l->SetComputeRootSignature(root.Get());
            l->SetPipelineState(ps.Get());
            unsigned params[] = {count, 0, 0, 0};
            l->SetComputeRoot32BitConstants(0, 4, params, 0);
            l->SetComputeRootShaderResourceView(1, a.address());
            l->SetComputeRootUnorderedAccessView(2, b.address());
            l->Dispatch(count, 1, 1);
            guidegpu::uavBarrier(l, b.resource.Get());
            rb = b.readback(c.device.Get(), l);
        });
        auto bytes = guidegpu::mappedBytes(rb.Get(), count * 64);
        unsigned bitErrors = 0, flagErrors = 0, arithmeticErrors = 0;
        double sqrtError = 0, addError = 0, mulError = 0, divError = 0;
        std::ostringstream csv;
        csv << std::setprecision(17) << "x,cpuSqrt,gpuSqrt,relative\n";
        for (unsigned i = 0; i < count; i++)
        {
            double result[5];
            std::memcpy(result, bytes.data() + i * 64, 40);
            uint32_t flags[4];
            std::memcpy(flags, bytes.data() + i * 64 + 40, 16);
            double x = v[i * 2], y = v[i * 2 + 1];
            bitErrors += std::bit_cast<uint64_t>(x) != std::bit_cast<uint64_t>(result[0]);
            flagErrors += flags[0] != uint32_t(x > 0) || flags[1] != uint32_t(std::isfinite(x)) ||
                          flags[2] != uint32_t(x == y) || flags[3] != 0x1234abcd;
            auto check = [&](double actual, double expected, double &maximum) {
                if (std::isnan(expected))
                {
                    arithmeticErrors += !std::isnan(actual);
                    return;
                }
                if (!std::isfinite(expected))
                {
                    arithmeticErrors += actual != expected;
                    return;
                }
                if (!std::isfinite(actual))
                {
                    arithmeticErrors++;
                    return;
                }
                double e =
                    std::abs(actual - expected) / std::max(std::abs(expected), std::numeric_limits<double>::min());
                maximum = std::max(maximum, e);
            };
            check(result[1], x + y, addError);
            check(result[2], x * y, mulError);
            check(result[3], x / y, divError);
            double sr = std::sqrt(x);
            check(result[4], sr, sqrtError);
            double sy;
            std::memcpy(&sy, bytes.data() + i * 64 + 56, 8);
            check(sy, std::sqrt(y), sqrtError);
            double rel = std::isfinite(sr) && sr > 0 ? std::abs(result[4] - sr) / sr : 0;
            csv << x << ',' << sr << ',' << result[4] << ',' << rel << '\n';
        }
        bool passed = bitErrors == 0 && flagErrors == 0 && arithmeticErrors == 0 && sqrtError < 5e-16 &&
                      addError < 5e-16 && mulError < 5e-16 && divError < 5e-16;
        gpuls::text(out / "sqrt.csv", csv.str());
        std::ostringstream summary;
        summary << std::setprecision(17) << "{\"unsafeIntrinsic\":" << (unsafe ? "true" : "false")
                << ",\"cases\":" << count << ",\"minActualPivot\":" << minPivot << ",\"maxActualPivot\":" << maxPivot
                << ",\"bitErrors\":" << bitErrors << ",\"flagErrors\":" << flagErrors
                << ",\"arithmeticErrors\":" << arithmeticErrors << ",\"sqrtMaxRelative\":" << sqrtError
                << ",\"addMaxRelative\":" << addError << ",\"mulMaxRelative\":" << mulError
                << ",\"divMaxRelative\":" << divError << ",\"pass\":" << (passed ? "true" : "false") << '}';
        gpuls::text(out / "STATUS.json", summary.str());
        c.audit();
        std::cout << c.json() << '\n' << summary.str() << '\n';
        return passed ? 0 : 1;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 2;
    }
}
