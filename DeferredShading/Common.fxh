//--------------------------------------------------------------------------------------
// Copyright 2011 Intel Corporation
// All Rights Reserved
//
// Permission is granted to use, copy, distribute and prepare derivative works of this
// software for any purpose and without fee, provided, that the above copyright notice
// and this statement appear in all copies.  Intel makes no representations about the
// suitability of this software for any purpose.  THIS SOFTWARE IS PROVIDED "AS IS."
// INTEL SPECIFICALLY DISCLAIMS ALL WARRANTIES, EXPRESS OR IMPLIED, AND ALL LIABILITY,
// INCLUDING CONSEQUENTIAL AND OTHER INDIRECT DAMAGES, FOR THE USE OF THIS SOFTWARE,
// INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PROPRIETARY RIGHTS, AND INCLUDING THE
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  Intel does not
// assume any responsibility for any errors which may appear in this software nor any
// responsibility to update it.
//--------------------------------------------------------------------------------------

#include "Structures.fxh"

#define FLT_MAX 3.402823466e+38f

cbuffer cbPostProcessingAttribs : register( b0 )
{
    SPostProcessingAttribs g_PPAttribs;
};

cbuffer cbParticipatingMediaScatteringParams : register( b1 )
{
    SParticipatingMediaScatteringParams g_MediaParams;
}

// Frame parameters
cbuffer cbCameraAttribs : register( b2 )
{
    SCameraAttribs g_CameraAttribs;
}

cbuffer cbLightParams : register( b3 )
{
    SLightAttribs g_LightAttribs;
}

cbuffer cbMiscDynamicParams : register( b4 )
{
    SMiscDynamicParams g_MiscParams;
}

Texture2D<float>  g_tex2DDepthBuffer            : register( t0 );
Texture2D<float>  g_tex2DCamSpaceZ              : register( t0 );
Texture2D<float2> g_tex2DCoordinates            : register( t1 );
Texture2D<float>  g_tex2DEpipolarCamSpaceZ      : register( t2 );
Texture2D<float4> g_tex2DEpipolarSampleWorldPos : register( t0 );
Texture2D<uint2>  g_tex2DInterpolationSource    : register( t7 );
Texture2D<float>  g_tex2DLightSpaceDepthMap     : register( t3 );
Texture2D<float2> g_tex2DSliceUVDirInShadowMap  : register( t2 );
Texture2D<float2> g_tex2DMinMaxLightSpaceDepth  : register( t4 );
Texture2D<float4> g_tex2DStainedGlassColorDepth : register( t5 );
Texture2D<float3> g_tex2DInitialInsctrIrradiance: register( t6 );
Texture2D<float4> g_tex2DColorBuffer            : register( t1 );
Texture2D<float3> g_tex2DScatteredColor         : register( t3 );
Texture2D<float3> g_tex2DDownscaledInsctrRadiance: register( t2 );

float3 ApplyPhaseFunction(in float3 f3InsctrIntegral, in float3 f3EyeVector)
{
    //    sun
    //      \
    //       \
    //    ----\------eye
    //         \theta 
    //          \
    //    
    // compute cosine of theta angle
    float cosTheta = dot(f3EyeVector, g_LightAttribs.f4DirectionOnSun.xyz);
    
    // Compute Rayleigh scattering Phase Function
    // According to formula for the Rayleigh Scattering phase function presented in the 
    // "Rendering Outdoor Light Scattering in Real Time" by Hoffman and Preetham (see p.36 and p.51), 
    // BethaR(Theta) is calculated as follows:
    // 3/(16PI) * BethaR * (1+cos^2(theta))
    // g_MediaParams.f4AngularRayleighBeta == (3*PI/16) * g_MediaParams.f4TotalRayleighBeta, hence:
    float3 RayleighScatteringPhaseFunc = g_MediaParams.f4AngularRayleighBeta.rgb * (1.0 + cosTheta*cosTheta);

    // Compute Henyey-Greenstein approximation of the Mie scattering Phase Function
    // According to formula for the Mie Scattering phase function presented in the 
    // "Rendering Outdoor Light Scattering in Real Time" by Hoffman and Preetham 
    // (see p.38 and p.51),  BethaR(Theta) is calculated as follows:
    // 1/(4PI) * BethaM * (1-g^2)/(1+g^2-2g*cos(theta))^(3/2)
    // const float4 g_MediaParams.f4HG_g = float4(1 - g*g, 1 + g*g, -2*g, 1);
    float HGTemp = rsqrt( dot(g_MediaParams.f4HG_g.yz, float2(1.f, cosTheta)) );//rsqrt( g_MediaParams.f4HG_g.y + g_MediaParams.f4HG_g.z*cosTheta);
    // g_MediaParams.f4AngularMieBeta is calculated according to formula presented in "A practical Analytic 
    // Model for Daylight" by Preetham & Hoffman (see p.23)
    float3 fMieScatteringPhaseFunc_HGApprox = g_MediaParams.f4AngularMieBeta.rgb * g_MediaParams.f4HG_g.x * (HGTemp*HGTemp*HGTemp);

    float3 f3InscatteredLight = f3InsctrIntegral * 
                               (RayleighScatteringPhaseFunc + fMieScatteringPhaseFunc_HGApprox);

    f3InscatteredLight.rgb *= g_LightAttribs.f4SunColorAndIntensityAtGround.w;  
    
    return f3InscatteredLight;
}

float3 ProjSpaceXYZToWorldSpace(in float3 f3PosPS)
{
    // We need to compute depth before applying view-proj inverse matrix
    float fDepth = g_CameraAttribs.mProj[2][2] + g_CameraAttribs.mProj[3][2] / f3PosPS.z;
    float4 ReconstructedPosWS = mul( float4(f3PosPS.xy,fDepth,1), g_CameraAttribs.mViewProjInv );
    ReconstructedPosWS /= ReconstructedPosWS.w;
    return ReconstructedPosWS.xyz;
}

void GetTracingAttribs(inout float3 f3RayEndPosWS, out float3 f3EyeVector, out float fRayLength)
{
    f3EyeVector = f3RayEndPosWS.xyz - g_CameraAttribs.f4CameraPos.xyz;
    fRayLength = length(f3EyeVector);
    f3EyeVector /= fRayLength;
    fRayLength = min(fRayLength, g_PPAttribs.m_fMaxTracingDistance);
    // Update end position
    f3RayEndPosWS = g_CameraAttribs.f4CameraPos.xyz + fRayLength * f3EyeVector;
}