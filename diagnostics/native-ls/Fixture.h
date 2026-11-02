#pragma once
#include "NativeLS.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <sstream>
namespace fs = std::filesystem;
using hair::require;
struct Fixture
{
    std::string text;
    unsigned n = 0, rows = 0;
    std::vector<int> mx, mq;
    explicit Fixture(const fs::path &p)
    {
        std::ifstream f(p);
        text.assign(std::istreambuf_iterator<char>(f), {});
        require(!text.empty(), "Missing fixture");
        n = number("n");
        rows = number("rows");
        mx = ints("mapX");
        mq = ints("mapQ");
    }
    size_t key(const std::string &k) const
    {
        auto p = text.find('"' + k + '"');
        require(p != std::string::npos, "Missing key " + k);
        return text.find(':', p) + 1;
    }
    unsigned number(const std::string &k) const
    {
        return unsigned(std::stoul(text.substr(key(k))));
    }
    std::vector<int> ints(const std::string &k) const
    {
        auto p = text.find('[', key(k)) + 1, end = text.find(']', p);
        std::vector<int> v;
        while (p < end)
        {
            while (p < end && (text[p] == ' ' || text[p] == ','))
                p++;
            if (p == end)
                break;
            size_t count;
            v.push_back(std::stoi(text.substr(p), &count));
            p += count;
        }
        return v;
    }
    std::vector<double> bits(const std::string &k) const
    {
        auto p = text.find('[', key(k)) + 1, end = text.find(']', p);
        std::vector<double> v;
        while ((p = text.find('"', p)) < end)
        {
            auto next = text.find('"', p + 1);
            auto a = std::stoull(text.substr(p + 1, next - p - 1), nullptr, 16);
            v.push_back(std::bit_cast<double>(uint64_t(a)));
            p = next + 1;
        }
        return v;
    }
};

GuideCpuState dummy(const Fixture &);
void import(nativels::Workspace &, const Fixture &);
