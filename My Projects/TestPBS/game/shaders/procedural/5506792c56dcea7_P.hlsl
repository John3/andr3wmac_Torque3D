//*****************************************************************************
// Torque -- HLSL procedural shader
//*****************************************************************************

// Dependencies:
#include "shaders/common/torque.hlsl"

// Features:
// Vert Position
// Deferred Shading: Diffuse Map
// Deferred Shading: Specular Color
// Deferred Shading: Specular Power
// Deferred Shading: Specular Strength
// Deferred Shading: Mat Info Flags
// Visibility
// Eye Space Depth (Out)
// GBuffer Conditioner
// Hardware Instancing

struct ConnectData
{
   float2 texCoord        : TEXCOORD0;
   float visibility      : TEXCOORD1;
   float2 vpos            : VPOS;
   float4 wsEyeVec        : TEXCOORD2;
   float3 gbNormal        : TEXCOORD3;
};


struct Fragout
{
   float4 col : COLOR0;
   float4 col1 : COLOR1;
   float4 col2 : COLOR2;
};


//-----------------------------------------------------------------------------
// Main
//-----------------------------------------------------------------------------
Fragout main( ConnectData IN,
              uniform sampler2D diffuseMap      : register(S0),
              uniform float4    specularColor   : register(C0),
              uniform float     specularPower   : register(C1),
              uniform float     specularStrength : register(C2),
              uniform float     matInfoFlags    : register(C3),
              uniform float3    vEye            : register(C4),
              uniform float4    oneOverFarplane : register(C5)
)
{
   Fragout OUT;

   // Vert Position
   
   // Deferred Shading: Diffuse Map
   OUT.col1 = tex2DLinear(diffuseMap, IN.texCoord);
   
   // Deferred Shading: Specular Color
   OUT.col1.a = specularColor.r;
   
   // Deferred Shading: Specular Power
   OUT.col2 = float4(0.0, 0.0, specularPower / 128.0, 0.0);
   
   // Deferred Shading: Specular Strength
   OUT.col2.a = specularStrength / 5.0;
   
   // Deferred Shading: Mat Info Flags
   OUT.col2.r = matInfoFlags;
   
   // Visibility
   fizzle( IN.vpos, IN.visibility );
   
   // Eye Space Depth (Out)
#ifndef CUBE_SHADOW_MAP
   float eyeSpaceDepth = dot(vEye, (IN.wsEyeVec.xyz / IN.wsEyeVec.w));
#else
   float eyeSpaceDepth = length( IN.wsEyeVec.xyz / IN.wsEyeVec.w ) * oneOverFarplane.x;
#endif
   
   // GBuffer Conditioner
   float4 normal_depth = float4(normalize(IN.gbNormal), eyeSpaceDepth);

   // output buffer format: GFXFormatR16G16B16A16F
   // g-buffer conditioner: float4(normal.X, normal.Y, depth Hi, depth Lo)
   float4 _gbConditionedOutput = float4(sqrt(half(2.0/(1.0 - normal_depth.y))) * half2(normal_depth.xz), 0.0, normal_depth.a);
   
   // Encode depth into hi/lo
   float2 _tempDepth = frac(normal_depth.a * float2(1.0, 65535.0));
   _gbConditionedOutput.zw = _tempDepth.xy - _tempDepth.yy * float2(1.0/65535.0, 0.0);

   OUT.col = _gbConditionedOutput;
   
   // Hardware Instancing
   

   return OUT;
}
