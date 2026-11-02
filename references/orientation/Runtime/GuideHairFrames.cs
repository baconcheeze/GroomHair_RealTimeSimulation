using System;
using UnityEngine;

namespace HairRest
{
    // Shared material convention: local +Z is tangent. Quaternions are xyzw.
    public static class GuideHairFrames
    {
        public static Quaternion First(Vector3 tangent)
        {
            Vector3 reference = Mathf.Abs(tangent.x) < 0.8f ? Vector3.right : Vector3.up;
            Vector3 d1 = (reference - Vector3.Dot(reference, tangent) * tangent).normalized;
            return Quaternion.LookRotation(tangent, Vector3.Cross(tangent, d1)).normalized;
        }
        public static Quaternion Transport(Quaternion previous, Vector3 tangent)
        {
            Vector3 old = previous * Vector3.forward;
            float dot = Mathf.Clamp(Vector3.Dot(old, tangent), -1, 1);
            Quaternion rotation;
            if (dot < -0.999999f)
            {
                // At an exact U-turn the minimal axis is ambiguous: use transported d1.
                rotation = Quaternion.AngleAxis(180, previous * Vector3.right);
            }
            else
            {
                Vector3 cross = Vector3.Cross(old, tangent);
                rotation = new Quaternion(cross.x, cross.y, cross.z, 1 + dot).normalized;
            }
            Quaternion transported = (rotation * previous).normalized;
            // Reproject to the exact new tangent, including the nearly-opposite case.
            Vector3 d1 = transported * Vector3.right;
            d1 = (d1 - Vector3.Dot(d1, tangent) * tangent).normalized;
            Quaternion q = Quaternion.LookRotation(tangent, Vector3.Cross(tangent, d1)).normalized;
            return Quaternion.Dot(previous, q) < 0 ? Negate(q) : q;
        }
        public static Quaternion Negate(Quaternion q) => new Quaternion(-q.x, -q.y, -q.z, -q.w);
        public static Vector3 RotationVector(Quaternion a, Quaternion b)
        {
            Quaternion q = (Quaternion.Inverse(a) * b).normalized;
            if (q.w < 0) q = Negate(q);
            Vector3 v = new Vector3(q.x, q.y, q.z);
            float s = v.magnitude;
            return s < 1e-7f ? 2 * v : v * (2 * Mathf.Atan2(s, Mathf.Max(0, q.w)) / s);
        }
        // Stable Cosserat Rods (SIGGRAPH 2025), Eq. (3)-(5): store full
        // intrinsic relative quaternion, not a different log-curvature energy.
        // Runtime contract, evaluated by StableCosseratOrientation (not this utility): qRel = inverse(q_i)*q_j,
        // q0 = stored rest target; dPlus = |qRel-q0|^2, dMinus = |qRel+q0|^2;
        // phi = dPlus <= dMinus ? +1 : -1; C_bt = qRel - phi*q0.
        // Rest-frame sign continuity above does NOT replace this runtime closest-pole
        // selection. No persistent phi buffer is required.
        public static Quaternion BendTwist(Quaternion a, Quaternion b)
        {
            Quaternion q = (Quaternion.Inverse(a) * b).normalized;
            return q.w < 0 ? Negate(q) : q;
        }
        public static float AngleRadians(Quaternion a, Quaternion b)
            => RotationVector(a, b).magnitude;
    }
}
