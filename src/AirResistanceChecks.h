#pragma once
#include <iosfwd>
namespace JPH { class ComputeSystem; class ComputeQueue; }
void RunAirResistanceChecks(JPH::ComputeSystem *,JPH::ComputeQueue *,std::ostream &);
