using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace HairRest
{
    [StructLayout(LayoutKind.Sequential)]
    public struct GuideStrandRange
    {
        public uint firstVertex, vertexCount, firstSegment, segmentCount, firstBend, bendCount;
    }
    internal static class GuideBuffer
    {
        internal static GraphicsBuffer Upload<T>(T[] data, int stride) where T : struct
        {
            // A groom made exclusively of two-vertex strands has zero bends.
            if (data.Length == 0) return null;
            var buffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, data.Length, stride);
            try { buffer.SetData(data); return buffer; } catch { buffer.Dispose(); throw; }
        }
        internal static T[] Read<T>(GraphicsBuffer buffer, int count) where T : struct
        { var data = new T[count]; if (count > 0) buffer.GetData(data); return data; }
    }
    public sealed class GuideHairTopology : IDisposable
    {
        internal readonly GuideStrandRange[] Ranges;
        internal GraphicsBuffer Buffer { get; private set; }
        // Existing debug renderer's derived segment->vertex lookup; not solver topology.
        internal GraphicsBuffer DebugSegmentStarts { get; private set; }
        public int StrandCount => Ranges.Length;
        public int VertexCount { get; }
        public int SegmentCount { get; }
        public int BendCount { get; }
        internal GuideHairTopology(StrandRestData source)
        {
            VertexCount = source.VertexCount;
            Ranges = new GuideStrandRange[source.StrandCount];
            int segment = 0, bend = 0;
            for (int s = 0; s < Ranges.Length; ++s)
            {
                int count = source.GetStrandVertexCount(s);
                if (count < 2)
                    throw new InvalidOperationException($"Guide strand {s} has only {count} vertices.");
                int segmentCount = count - 1;
                int bendCount = Math.Max(0, count - 2);
                Ranges[s] = new GuideStrandRange { firstVertex = (uint)source.GetStrandStart(s), vertexCount = (uint)count,
                    firstSegment = (uint)segment, segmentCount = (uint)segmentCount, firstBend = (uint)bend, bendCount = (uint)bendCount };
                segment += segmentCount; bend += bendCount;
            }
            SegmentCount = segment; BendCount = bend;
            try { Buffer = GuideBuffer.Upload(Ranges, 24); DebugSegmentStarts = GuideBuffer.Upload(source.CreateSegmentStarts(), 4); }
            catch { Dispose(); throw; }
        }
        public void Dispose() { Buffer?.Dispose(); Buffer = null; DebugSegmentStarts?.Dispose(); DebugSegmentStarts = null; }
    }
    public sealed class GuideHairRestState : IDisposable
    {
        // No public mutable handles or arrays: rest data has constructor-only uploads.
        internal GraphicsBuffer Position, Orientation, Length, BendTwist, Mass, Radius;
        internal readonly Vector3[] Positions;
        internal readonly Quaternion[] Orientations, Bends;
        internal readonly float[] Lengths, Masses, Radii;
        public const float PhysicalRadius = 0.00004f; // temporary 40 micrometers
        public const float Density = 1300f; // temporary kg/m^3, physical single strand
        public const float LengthEpsilon = 1e-7f;
        internal GuideHairRestState(StrandRestData source, GuideHairTopology topology)
        {
            Positions = source.CopyPositions(); Orientations = new Quaternion[topology.SegmentCount];
            Lengths = new float[topology.SegmentCount]; Radii = new float[topology.SegmentCount];
            Masses = new float[topology.VertexCount]; Bends = new Quaternion[topology.BendCount];
            int degenerate = 0;
            foreach (var r in topology.Ranges)
            {
                Quaternion q = Quaternion.identity;
                for (int j = 0; j < r.segmentCount; ++j)
                {
                    int v = (int)r.firstVertex + j, s = (int)r.firstSegment + j;
                    Vector3 edge = Positions[v + 1] - Positions[v]; float length = edge.magnitude;
                    if (!(length > LengthEpsilon) || float.IsInfinity(length)) { ++degenerate; continue; }
                    Lengths[s] = length;
                    Radii[s] = PhysicalRadius;

                    q = j == 0 ? GuideHairFrames.First(edge / length) : GuideHairFrames.Transport(q, edge / length);
                    Orientations[s] = q;

                    float halfMass = 0.5f * Density * Mathf.PI * PhysicalRadius * PhysicalRadius * length;
                    Masses[v] += halfMass; Masses[v + 1] += halfMass;

                    if (j > 0) Bends[(int)r.firstBend + j - 1] = GuideHairFrames.BendTwist(Orientations[s - 1], q);
                }
            }
            if (degenerate > 0) throw new InvalidOperationException($"Guide rest initialization rejected: degenerate segment count={degenerate}, epsilon={LengthEpsilon:R} m");
            try
            {
                Position = GuideBuffer.Upload(Positions, 12); Orientation = GuideBuffer.Upload(Orientations, 16);
                Length = GuideBuffer.Upload(Lengths, 4); BendTwist = GuideBuffer.Upload(Bends, 16);
                Mass = GuideBuffer.Upload(Masses, 4); Radius = GuideBuffer.Upload(Radii, 4);
            }
            catch { Dispose(); throw; }
        }
        public void Dispose()
        {
            Position?.Dispose(); Orientation?.Dispose(); Length?.Dispose(); BendTwist?.Dispose(); Mass?.Dispose(); Radius?.Dispose();
            Position = Orientation = Length = BendTwist = Mass = Radius = null;
        }
    }
    public sealed class GuideHairDynamicState : IDisposable
    {
        internal GraphicsBuffer Position, Velocity, Orientation;
        internal GuideHairDynamicState(GuideHairRestState rest)
        {
            try { Position = GuideBuffer.Upload(rest.Positions, 12); Velocity = GuideBuffer.Upload(new Vector3[rest.Positions.Length], 12);
                Orientation = GuideBuffer.Upload(rest.Orientations, 16); }
            catch { Dispose(); throw; }
        }
        public void Dispose() { Position?.Dispose(); Velocity?.Dispose(); Orientation?.Dispose(); Position = Velocity = Orientation = null; }
    }
    // Future solver owns work arrays and force/torque outputs. No allocations or
    // inertial orientation prediction in this phase. Collision/grid owners remain separate.
    public sealed class GuideHairSolverScratch : IDisposable { public void Dispose() { } }
}
