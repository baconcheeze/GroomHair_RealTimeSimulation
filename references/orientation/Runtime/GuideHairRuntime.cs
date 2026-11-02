using System;
using UnityEngine;

namespace HairRest
{
    public sealed class GuideHairDiagnostics
    {
        public int Strands, Vertices, Segments, Bends, Degenerate, SignFlips;
        public float MinLength, MaxLength, AverageLength, MaxVelocity, MaxPosition, RmsPosition;
        public float MaxOrientation, MaxTangent, MaxNormalization, MaxShearStretch, MaxBendTwist;
        public override string ToString() => FormattableString.Invariant(
            $"Guide state: strands={Strands}, vertices={Vertices}, segments={Segments}, bends={Bends}\nLength m: min={MinLength:R}, max={MaxLength:R}, average={AverageLength:R}, degenerate={Degenerate}\nPosition max={MaxPosition:R} m, RMS={RmsPosition:R} m; velocity max={MaxVelocity:R} m/s; orientation max={MaxOrientation:R} rad\nTangent max={MaxTangent:R} rad; quaternion normalization max={MaxNormalization:R}; sign flips={SignFlips}\nCanonical rest shear/stretch max |C_ss|={MaxShearStretch:R}; bend/twist reconstruction max={MaxBendTwist:R} rad");
    }
    public sealed class GuideHairRuntime : IDisposable
    {
        public GuideHairTopology Topology { get; private set; }
        public GuideHairRestState Rest { get; private set; }
        public GuideHairDynamicState State { get; private set; }
        public GuideHairSolverScratch Scratch { get; private set; }
        public GuideHairDiagnostics Diagnostics { get; private set; }
        public int LineVertexCount => checked(Topology.SegmentCount * 2);
        public GuideHairRuntime(StrandRestData source)
        {
            try { Topology = new GuideHairTopology(source); Rest = new GuideHairRestState(source, Topology);
                State = new GuideHairDynamicState(Rest); Scratch = new GuideHairSolverScratch(); Validate(source); }
            catch { Dispose(); throw; }
        }
        public void Bind(MaterialPropertyBlock properties)
        {
            properties.SetBuffer("_CurrentPositions", State.Position);
            properties.SetBuffer("_SegmentStarts", Topology.DebugSegmentStarts);
        }
        static void Require(bool value, string message) { if (!value) throw new InvalidOperationException("Guide validation: " + message); }
        static void Exact<T>(T[] actual, T[] expected, string label) where T : struct
        {
            Require(actual.Length == expected.Length, label + " count");
            for (int i = 0; i < actual.Length; ++i) Require(actual[i].Equals(expected[i]), $"{label}[{i}] changed");
        }
        public GuideHairDiagnostics Validate(StrandRestData source, bool requireRestOrientation = true)
        {
            var t = Topology;
            var d = new GuideHairDiagnostics { Strands = t.StrandCount, Vertices = t.VertexCount, Segments = t.SegmentCount,
                Bends = t.BendCount, MinLength = float.PositiveInfinity };
            Require(t.StrandCount == source.StrandCount && t.VertexCount == source.VertexCount && t.SegmentCount == source.SegmentCount, "source topology counts");
            Require(t.SegmentCount == t.VertexCount - t.StrandCount && t.BendCount == t.SegmentCount - t.StrandCount, "packed topology count identities");
            Exact(GuideBuffer.Read<GuideStrandRange>(t.Buffer, t.StrandCount), t.Ranges, "topology");
            Exact(GuideBuffer.Read<uint>(t.DebugSegmentStarts, t.SegmentCount), source.CreateSegmentStarts(), "debug connectivity");
            var p = GuideBuffer.Read<Vector3>(Rest.Position, t.VertexCount);
            var x = GuideBuffer.Read<Vector3>(State.Position, t.VertexCount);
            var velocity = GuideBuffer.Read<Vector3>(State.Velocity, t.VertexCount);
            var q = GuideBuffer.Read<Quaternion>(Rest.Orientation, t.SegmentCount);
            var currentQ = GuideBuffer.Read<Quaternion>(State.Orientation, t.SegmentCount);
            var lengths = GuideBuffer.Read<float>(Rest.Length, t.SegmentCount);
            var bends = GuideBuffer.Read<Quaternion>(Rest.BendTwist, t.BendCount);
            var mass = GuideBuffer.Read<float>(Rest.Mass, t.VertexCount);
            var radii = GuideBuffer.Read<float>(Rest.Radius, t.SegmentCount);
            Exact(p, source.CopyPositions(), "rest positions"); Exact(q, Rest.Orientations, "rest orientations");
            Exact(lengths, Rest.Lengths, "rest lengths"); Exact(bends, Rest.Bends, "rest bend/twist");
            Exact(mass, Rest.Masses, "mass"); Exact(radii, Rest.Radii, "radius");
            double squaredError = 0, totalLength = 0;
            for (int v = 0; v < x.Length; ++v)
            {
                float error = Vector3.Distance(x[v], p[v]); d.MaxPosition = Mathf.Max(d.MaxPosition, error); squaredError += (double)error * error;
                d.MaxVelocity = Mathf.Max(d.MaxVelocity, velocity[v].magnitude);
                Require(x[v].Equals(p[v]) && velocity[v].Equals(Vector3.zero), "initial positions/zero velocity");
                Require(mass[v] > 0 && !float.IsInfinity(mass[v]), "positive physical mass including roots");
            }
            d.RmsPosition = (float)Math.Sqrt(squaredError / x.Length);
            foreach (var r in t.Ranges)
                for (int j = 0; j < r.segmentCount; ++j)
                {
                    int s = (int)r.firstSegment + j, v = (int)r.firstVertex + j;
                    float l = lengths[s]; if (!(l > GuideHairRestState.LengthEpsilon)) ++d.Degenerate;
                    d.MinLength = Mathf.Min(d.MinLength, l); d.MaxLength = Mathf.Max(d.MaxLength, l); totalLength += l;
                    float norm = Mathf.Sqrt(Quaternion.Dot(q[s], q[s]));
                    Require(Mathf.Abs(Mathf.Sqrt(Quaternion.Dot(currentQ[s], currentQ[s])) - 1) < 2e-6f, "current unit quaternion");
                    d.MaxNormalization = Mathf.Max(d.MaxNormalization, Mathf.Abs(norm - 1));
                    Vector3 tangent = (p[v + 1] - p[v]).normalized, material = (q[s] * Vector3.forward).normalized;
                    d.MaxTangent = Mathf.Max(d.MaxTangent, Mathf.Atan2(Vector3.Cross(tangent, material).magnitude, Vector3.Dot(tangent, material)));
                    // Canonical world-space C_ss = edge/L0 - d3 (all vectors in
                    // the same Unity neck-local spatial frame). Do not renormalize d3.
                    Vector3 restConstraint = (p[v + 1] - p[v]) / l - q[s] * Vector3.forward;
                    d.MaxShearStretch = Mathf.Max(d.MaxShearStretch, restConstraint.magnitude);
                    float angle = currentQ[s].Equals(q[s]) || currentQ[s].Equals(GuideHairFrames.Negate(q[s])) ? 0 : GuideHairFrames.AngleRadians(q[s], currentQ[s]);
                    d.MaxOrientation = Mathf.Max(d.MaxOrientation, angle);
                    if (requireRestOrientation) Require(angle < 1e-6f, "static orientation");
                    if (j > 0)
                    {
                        if (Quaternion.Dot(q[s - 1], q[s]) < -1e-6f) ++d.SignFlips;
                        Quaternion intrinsic = bends[(int)r.firstBend + j - 1];
                        Quaternion reconstructed = GuideHairFrames.BendTwist(q[s - 1], q[s]);
                        float error = reconstructed.Equals(intrinsic) ? 0 : GuideHairFrames.AngleRadians(reconstructed, intrinsic);
                        d.MaxBendTwist = Mathf.Max(d.MaxBendTwist, error);
                    }
                }
            d.AverageLength = (float)(totalLength / t.SegmentCount);
            Require(d.Degenerate == 0 && d.SignFlips == 0 && d.MaxTangent < 2e-5f && d.MaxNormalization < 2e-6f &&
                d.MaxShearStretch < 2e-5f && d.MaxBendTwist < 1e-5f, "frame/rest strain tolerances");
            Diagnostics = d; return d;
        }
        public void Dispose()
        {
            Scratch?.Dispose(); State?.Dispose(); Rest?.Dispose(); Topology?.Dispose();
            Scratch = null; State = null; Rest = null; Topology = null;
        }
    }
}
