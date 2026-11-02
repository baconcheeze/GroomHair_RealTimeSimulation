#include "ShaderCore.h"
JPH_SHADER_BIND_BEGIN(JPH_HairAirResistanceProbe)
    JPH_SHADER_BIND_BUFFER(JPH_float4, gInput)
    JPH_SHADER_BIND_RW_BUFFER(JPH_float4, gOutput)
JPH_SHADER_BIND_END(JPH_HairAirResistanceProbe)
