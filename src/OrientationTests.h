#pragma once
#include "OrientationDiagnostics.h"
struct OrientationTestContext {
    ID3D12Device* device;
    Submit submit;
    std::filesystem::path shaders,report;
    bool software=false;
};
void testGuideState(guidegpu::State& state,OrientationSolver& solver,const OrientationTestContext& context);
void testOrientationLocal(const OrientationTestContext& context);
void testOrientationGroom(guidegpu::State& state,OrientationSolver& solver,const OrientationTestContext& context);
