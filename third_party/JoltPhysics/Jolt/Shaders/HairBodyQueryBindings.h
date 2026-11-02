#include "HairBodyStructs.h"
JPH_SHADER_BIND_BEGIN(JPH_HairBodyQuery)
 JPH_SHADER_BIND_BUFFER(JPH_float4, gBodyState)
 JPH_SHADER_BIND_BUFFER(JPH_uint, gBodyField)
 JPH_SHADER_BIND_BUFFER(JPH_HairPosition, gPositions)
 JPH_SHADER_BIND_RW_BUFFER(JPH_HairCollisionPlane, gCollisionPlanes)
JPH_SHADER_BIND_END(JPH_HairBodyQuery)
