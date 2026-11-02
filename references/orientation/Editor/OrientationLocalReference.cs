using System;
using UnityEngine;

namespace HairRest.Editor
{
    // Validation ONLY: dense symmetric Jacobi eigensolver + secular equation in double precision.
    // Does not use the shader's two-projector method or lambda fixed-point iteration.
    internal static class OrientationLocalReference
    {
        internal struct D
        {
            public double x,y,z,w;
            public D(double x,double y,double z,double w){this.x=x;this.y=y;this.z=z;this.w=w;}
            public D(Vector4 v):this(v.x,v.y,v.z,v.w){}
            public D(Quaternion v):this(v.x,v.y,v.z,v.w){}
            public double this[int i]{get=>i==0?x:i==1?y:i==2?z:w;set{if(i==0)x=value;else if(i==1)y=value;else if(i==2)z=value;else w=value;}}
            public double Norm=>Math.Sqrt(Dot(this));
            public D Unit=>this/Norm;
            public D Conj=>new D(-x,-y,-z,w);
            public Vector4 Float=>new Vector4((float)x,(float)y,(float)z,(float)w);
            public double Dot(D b)=>x*b.x+y*b.y+z*b.z+w*b.w;
            public static D operator+(D a,D b)=>new D(a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w);
            public static D operator-(D a,D b)=>a+(-1)*b;
            public static D operator*(double s,D a)=>new D(s*a.x,s*a.y,s*a.z,s*a.w);
            public static D operator/(D a,double s)=>(1/s)*a;
            public static D operator*(D a,D b)=>new D(a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z);
        }
        internal static D Apply(D v,D q)=>v*q*new D(0,0,1,0);
        internal static double Objective(D v,D b,D q)=>-.5*q.Dot(Apply(v,q))-b.Dot(q);
        internal static double Residual(D v,D b,D q,out double violation)
        {q=q.Unit;D s=Apply(v,q)+b;double l=q.Dot(s);violation=Math.Max(0,v.Norm-l);return (s-l*q).Norm;}
        internal static D Solve(D v,D b,D old)
        {
            double scale=Math.Max(v.Norm,b.Norm);if(scale==0)return old;
            v=v/scale;b=b/scale;
            // Explicit real xyzw matrix, independently checked against Hamilton products.
            var a=new double[,]{{v.z,0,-v.x,v.y},{0,v.z,-v.y,-v.x},{-v.x,-v.y,-v.z,0},{v.y,-v.x,0,-v.z}};
            var u=new double[4,4];for(int i=0;i<4;i++)u[i,i]=1;
            for(int step=0;step<128;step++)
            {
                int p=0,r=1;double largest=0;
                for(int i=0;i<4;i++)for(int j=i+1;j<4;j++)if(Math.Abs(a[i,j])>largest){largest=Math.Abs(a[i,j]);p=i;r=j;}
                if(largest<1e-15)break;
                double angle=.5*Math.Atan2(2*a[p,r],a[r,r]-a[p,p]),c=Math.Cos(angle),s=Math.Sin(angle);
                // Similarity J^T A J; columns p=(c,-s), r=(s,c).
                for(int i=0;i<4;i++){double ap=a[i,p],ar=a[i,r];a[i,p]=c*ap-s*ar;a[i,r]=s*ap+c*ar;}
                for(int i=0;i<4;i++){double ap=a[p,i],ar=a[r,i];a[p,i]=c*ap-s*ar;a[r,i]=s*ap+c*ar;}
                for(int i=0;i<4;i++){double up=u[i,p],ur=u[i,r];u[i,p]=c*up-s*ur;u[i,r]=s*up+c*ur;}
            }
            double top=double.NegativeInfinity;for(int i=0;i<4;i++)top=Math.Max(top,a[i,i]);
            var beta=new double[4];double topB2=0,lower2=0;
            for(int i=0;i<4;i++)
            {
                for(int j=0;j<4;j++)beta[i]+=u[j,i]*b[j];
                if(top-a[i,i]<1e-13)topB2+=beta[i]*beta[i];else lower2+=Math.Pow(beta[i]/(top-a[i,i]),2);
            }
            var coeff=new double[4];
            if(topB2<=1e-28*b.Dot(b)&&lower2<=1)
            {
                double projection2=0;for(int i=0;i<4;i++)if(top-a[i,i]<1e-13){for(int j=0;j<4;j++)coeff[i]+=u[j,i]*old[j];projection2+=coeff[i]*coeff[i];}
                if(projection2<1e-24){for(int i=0;i<4;i++)coeff[i]=0;for(int i=0;i<4;i++)if(top-a[i,i]<1e-13){coeff[i]=1;break;}projection2=1;}
                for(int i=0;i<4;i++)coeff[i]=top-a[i,i]<1e-13?coeff[i]*Math.Sqrt((1-lower2)/projection2):beta[i]/(top-a[i,i]);
            }
            else
            {
                double lo=0,hi=b.Norm;
                for(int n=0;n<200;n++)
                {
                    double gap=(lo+hi)*.5,total=0;
                    for(int i=0;i<4;i++){double den=gap+top-a[i,i];total+=Math.Pow(beta[i]/den,2);}
                    if(total>1)lo=gap;else hi=gap;
                }
                for(int i=0;i<4;i++)coeff[i]=beta[i]/(hi+top-a[i,i]);
            }
            D q=new D();for(int i=0;i<4;i++)for(int j=0;j<4;j++)q[i]+=u[i,j]*coeff[j];return q.Unit;
        }
        // Original constraints, not the quadratic objective; poles are recomputed here.
        internal static double PhysicalEnergy(D q,D edge,double length,double kss,D left,D right,D restLeft,D restRight,double kl,double kr)
        {
            D tangent=q*new D(0,0,1,0)*q.Conj,css=edge/length-tangent;
            double e=.5*kss*css.Dot(css);
            if(kl>0){D rel=left.Conj*q;D c=rel-(rel.Dot(restLeft)>=0?1:-1)*restLeft;e+=.5*kl*c.Dot(c);}
            if(kr>0){D rel=q.Conj*right;D c=rel-(rel.Dot(restRight)>=0?1:-1)*restRight;e+=.5*kr*c.Dot(c);}
            return e;
        }
    }
}
