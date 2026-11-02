#ifndef HAIR_BODY_STRUCTS
#define HAIR_BODY_STRUCTS
// HairLabDX12 GPU body primitives. MIT, same license as Jolt integration.
#include "HairStructs.h"
JPH_SHADER_CONSTANTS_BEGIN(JPH_BodyContext,gBodyContext)
 JPH_SHADER_CONSTANTS_MEMBER(JPH_uint,BodyCount)
 JPH_SHADER_CONSTANTS_MEMBER(JPH_float,BodyInvDt)
 JPH_SHADER_CONSTANTS_MEMBER(JPH_uint,BodyReset)
 JPH_SHADER_CONSTANTS_MEMBER(JPH_uint,BodyReserved)
JPH_SHADER_CONSTANTS_END(JPH_BodyContext)

#endif
