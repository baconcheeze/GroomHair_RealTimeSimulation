#pragma once
#include "RestData.h"
#include "QuaternionMath.h"
struct GuideRange { uint32_t firstVertex,vertexCount,firstSegment,segmentCount,firstBend,bendCount; };
static_assert(sizeof(GuideRange)==24 && offsetof(GuideRange,firstVertex)==0 && offsetof(GuideRange,vertexCount)==4 && offsetof(GuideRange,firstSegment)==8 && offsetof(GuideRange,segmentCount)==12 && offsetof(GuideRange,firstBend)==16 && offsetof(GuideRange,bendCount)==20);
static_assert(sizeof(Float3)==12 && offsetof(Float3,x)==0 && offsetof(Float3,y)==4 && offsetof(Float3,z)==8);
struct GuideInitialization {
    double minLength=DBL_MAX,maxLength{},averageLength{},maxNorm{},maxTangent{},maxSS{},maxBend{},minMass=DBL_MAX,maxMass{},totalMass{};
    std::string json() const;
};
struct GuideCpuState {
    const Strands* source{};
    std::vector<GuideRange> ranges;
    std::vector<hair::F> orientations,bends;
    std::vector<float> lengths,masses,radii;
    std::vector<Float3> velocities;
    GuideInitialization diagnostics;
    static constexpr float Radius=.00004f,Density=1300,LengthEpsilon=1e-7f;
    void initialize(const Strands& strands);
    std::vector<hair::F> perturb(const std::string& mode,unsigned participating,bool pinned,int seed=12345,float degrees=5) const;
};
