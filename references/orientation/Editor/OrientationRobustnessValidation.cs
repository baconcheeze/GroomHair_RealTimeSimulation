using System;
using System.IO;
using System.Text;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace HairRest.Editor
{
    public static partial class OrientationRobustnessValidation
    {
        public const string Output="Artifacts/OrientationRobustness";
        static readonly BindingFlags Hidden=BindingFlags.Instance|BindingFlags.NonPublic;
        static GraphicsBuffer StateBuffer(GuideHairRuntime r)=> (GraphicsBuffer)typeof(GuideHairDynamicState).GetField("Orientation",Hidden).GetValue(r.State);
        static HairRestData Fixture()
        {
            using var stream=new MemoryStream();using var w=new BinaryWriter(stream);
            foreach(uint n in new uint[]{0x5452484a,1,1,3,1,0,3})w.Write(n);
            // Loader reflects Z: negative SOURCE Z produces requested positive UNITY Z.
            foreach(float z in new[]{0f,-.01f,-.02f}){w.Write(0f);w.Write(0f);w.Write(z);}w.Write(0);
            return HairRestData.Load(stream.ToArray());
        }
        static void Require(bool ok,string text){if(!ok)throw new InvalidOperationException(text);}
        static Vector4 V(Quaternion q)=>new Vector4(q.x,q.y,q.z,q.w);
        static Quaternion Q(Vector4 q)=>new Quaternion(q.x,q.y,q.z,q.w);
        static Vector4[] Dispatch(string name,Vector4[] inputs)
        {
            var shader=UnityEngine.Object.Instantiate(AssetDatabase.LoadAssetAtPath<ComputeShader>("Assets/HairRest/Editor/OrientationLocalValidation.compute"));
            try
            {
                using var src=new GraphicsBuffer(GraphicsBuffer.Target.Structured,inputs.Length,16);
                using var dst=new GraphicsBuffer(GraphicsBuffer.Target.Structured,inputs.Length/3*4,16);
                src.SetData(inputs);int kernel=shader.FindKernel(name);shader.SetInt("_Count",inputs.Length/3);
                shader.SetBuffer(kernel,"_Inputs",src);shader.SetBuffer(kernel,"_Outputs",dst);shader.Dispatch(kernel,(inputs.Length/3+63)/64,1,1);
                var result=new Vector4[inputs.Length/3*4];dst.GetData(result);return result;
            }
            finally{UnityEngine.Object.DestroyImmediate(shader);}
        }
        static double ExactFixtureEnergy(Quaternion q)
        {
            Vector3 css=Vector3.forward-q*Vector3.forward;
            Quaternion rel=Quaternion.Inverse(q)*new Quaternion(1,0,0,0);
            double phi=rel.w>=0?1:-1;
            return .005*css.sqrMagnitude+.002*(rel.x*rel.x+rel.y*rel.y+rel.z*rel.z+(rel.w-phi)*(rel.w-phi));
        }
        public static void CaptureLegacyBatch()
        {
            try{CaptureLegacy();EditorApplication.Exit(0);}catch(Exception e){Debug.LogException(e);EditorApplication.Exit(1);}
        }
        static void CaptureLegacy()
        {
            Directory.CreateDirectory(Output);var log=new StringBuilder();var data=Fixture();
            using var runtime=new GuideHairRuntime(data.Guide);
            using var solver=new GuideHairOrientationSolver(runtime,new GuideOrientationSettings{pinRootOrientation=false},Resources.Load<ComputeShader>("StableCosseratOrientation"));
            var rest=solver.ReadOrientations();
            for(int i=0;i<3;i++)log.AppendLine($"Unity x{i}={data.Guide.GetRestPosition(i):F9}");
            for(int i=0;i<2;i++){log.AppendLine($"qRest{i}={rest[i]:F9}");Require(rest[i].Equals(Quaternion.identity),"Fixture rest must be identity");}
            var old=new Quaternion(1,0,0,0);StateBuffer(runtime).SetData(new[]{old,old});
            Vector3 edge=data.Guide.GetRestPosition(1)-data.Guide.GetRestPosition(0);
            Vector3 v=-2*edge;Vector4 b=(4e-5f/edge.magnitude)*V(old);
            var result=Dispatch("LegacyLocal",new[]{V(old),new Vector4(v.x,v.y,v.z,0),b});
            log.AppendLine($"First local frozen neighbor: v={v:F9}, b={b:F9}, a={result[3]:F9}");
            log.AppendLine($"initialLambda={result[1].x:R}, refinements={result[2].x}, finalLambda={result[1].y:R}, gap={result[1].z:R}, numeratorNorm={result[1].w:R}, valid={result[2].y}");
            double initial=ExactFixtureEnergy(old),target=ExactFixtureEnergy(new Quaternion(.1f,0,0,Mathf.Sqrt(.99f)));
            Require(Math.Abs(initial-.02)<1e-8&&Math.Abs(target-.0038)<1e-8,"Independent fixture energy values");
            log.AppendLine($"Constraint energy: old={initial:R}, example minimum={target:R}, legacy candidate={(result[2].y!=0?ExactFixtureEnergy(Q(result[0])).ToString("R"):"undefined")}");
            solver.Run(1);try{log.AppendLine("Actual original strand sweep: "+solver.Measure());}catch(Exception e){log.AppendLine("Actual original strand sweep: "+e.Message);}
            File.WriteAllText(Output+"/pre-patch.txt",log.ToString());Debug.Log(log.ToString());
        }
    }
}
