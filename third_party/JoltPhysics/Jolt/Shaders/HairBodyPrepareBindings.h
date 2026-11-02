#include "HairBodyStructs.h"
JPH_SHADER_BIND_BEGIN(JPH_HairBodyPrepare)
 JPH_SHADER_BIND_BUFFER(JPH_float4, gBodyInput)
 JPH_SHADER_BIND_BUFFER(JPH_Mat44, gScalpJointMatrices)
 JPH_SHADER_BIND_RW_BUFFER(JPH_float4, gBodyState)
 JPH_SHADER_BIND_RW_BUFFER(JPH_float3, gBodyWire)
JPH_SHADER_BIND_END(JPH_HairBodyPrepare)
