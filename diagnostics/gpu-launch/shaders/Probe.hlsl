#include "DoubleMath.hlsli"
ByteAddressBuffer Input:register(t0);
RWByteAddressBuffer Output:register(u0);
cbuffer Params:register(b0){uint Count,Base,Flag,Pad;};
double LoadD(uint p){uint2 b=Input.Load2(p);return asdouble(b.x,b.y);}
void StoreD(uint p,double x){uint lo,hi;asuint(x,lo,hi);Output.Store2(p,uint2(lo,hi));}
[numthreads(1,1,1)]void Probe(uint3 id:SV_DispatchThreadID){uint i=id.x;if(i>=Count)return;double x=LoadD(i*16),y=LoadD(i*16+8);uint p=i*64;
 StoreD(p,x);StoreD(p+8,x+y);StoreD(p+16,x*y);StoreD(p+24,x/y);
#ifdef UNSAFE_SQRT
 StoreD(p+32,sqrt(x));
#else
 StoreD(p+32,DSqrt(x));
#endif
 Output.Store(p+40,uint(x>0.0));Output.Store(p+44,uint(DFinite(x)));Output.Store(p+48,uint(x==y));Output.Store(p+52,0x1234abcdu);StoreD(p+56,DSqrt(y));}
