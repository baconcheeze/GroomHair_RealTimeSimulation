#pragma once
#include "WindowMath.h"
// Validation-only double manifold Gauss-Newton. Never linked into the application.
namespace autonomous {
struct ReferenceResult {bool qualified=false;unsigned iterations=0;double objective=0,positionCorrection=0,stationarity=0,spectral=0;};
ReferenceResult minimize(const GuideCpuState&,windowmath::State&,const std::vector<hair::D>& targets,const windowmath::Settings&,double positionTolerance=1e-10,double quaternionTolerance=1e-8);
double derivativeTest(const GuideCpuState&,const windowmath::Settings&);
}
