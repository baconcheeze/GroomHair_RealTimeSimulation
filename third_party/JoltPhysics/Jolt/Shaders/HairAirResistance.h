// HairLabDX12: linear drag relative to stationary world air.
#ifndef HAIRLAB_AIR_RESISTANCE_H
#define HAIRLAB_AIR_RESISTANCE_H

// Jolt's inertial velocity already includes the world-preserving component.
// Only add motion introduced by partial following of the moving hair frame.
float3 HairAirTransportVelocity(float3 oldLocal, float3 rebasedLocal,
                               float worldInfluence, float inverseFrameDt)
{
    return (1.0f-worldInfluence)*(oldLocal-rebasedLocal)*inverseFrameDt;
}

// gain = -expm1(-k * substep_dt), computed on CPU without small-k cancellation.
// Isotropic drag commutes with rotation; these vectors use the current hair basis.
float3 HairApplyAirResistance(float3 velocity, float3 transport, float gain)
{
    if (gain == 0.0f) return velocity;
    return velocity-gain*(velocity+transport);
}
#endif
