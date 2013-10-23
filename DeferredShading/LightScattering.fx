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

#include "Common.fxh"

#ifndef STAINED_GLASS
#   define STAINED_GLASS 1
#endif

#ifndef OPTIMIZE_SAMPLE_LOCATIONS
#   define OPTIMIZE_SAMPLE_LOCATIONS 1
#endif

#ifndef REFINE_INSCTR_INTEGRAL
#   define REFINE_INSCTR_INTEGRAL 1
#endif

#define SHADOW_MAP_DEPTH_BIAS 1e-4
//--------------------------------------------------------------------------------------
// Texture samplers
//--------------------------------------------------------------------------------------

SamplerState samLinearClamp : register( s0 )
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState samLinearBorder0 : register( s1 )
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Border;
    AddressV = Border;
    BorderColor = float4(0.0, 0.0, 0.0, 0.0);
};

//SamplerState samPointClamp
//{
//    Filter = MIN_MAG_MIP_POINT;
//    AddressU = CLAMP;
//    AddressV = CLAMP;
//};

SamplerState samLinearUClampVWrap : register( s2 )
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = CLAMP;
    AddressV = WRAP;
};

SamplerComparisonState samComparison : register( s3 )
{
    Filter = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    AddressU = Border;
    AddressV = Border;
    ComparisonFunc = GREATER;
    BorderColor = float4(0.0, 0.0, 0.0, 0.0);
};

//--------------------------------------------------------------------------------------
// Depth stencil states
//--------------------------------------------------------------------------------------

// Depth stencil state disabling depth test
DepthStencilState DSS_NoDepthTest
{
    DepthEnable = false;
    DepthWriteMask = ZERO;
};

DepthStencilState DSS_NoDepthTestIncrStencil
{
    DepthEnable = false;
    DepthWriteMask = ZERO;
    STENCILENABLE = true;
    FRONTFACESTENCILFUNC = ALWAYS;
    BACKFACESTENCILFUNC = ALWAYS;
    FRONTFACESTENCILPASS = INCR;
    BACKFACESTENCILPASS = INCR;
};

DepthStencilState DSS_NoDepth_StEqual_IncrStencil
{
    DepthEnable = false;
    DepthWriteMask = ZERO;
    STENCILENABLE = true;
    FRONTFACESTENCILFUNC = EQUAL;
    BACKFACESTENCILFUNC = EQUAL;
    FRONTFACESTENCILPASS = INCR;
    BACKFACESTENCILPASS = INCR;
    FRONTFACESTENCILFAIL = KEEP;
    BACKFACESTENCILFAIL = KEEP;
};


//--------------------------------------------------------------------------------------
// Rasterizer states
//--------------------------------------------------------------------------------------

// Rasterizer state for solid fill mode with no culling
RasterizerState RS_SolidFill_NoCull
{
    FILLMODE = Solid;
    CullMode = NONE;
};


// Blend state disabling blending
BlendState NoBlending
{
    BlendEnable[0] = FALSE;
    BlendEnable[1] = FALSE;
    BlendEnable[2] = FALSE;
};

float2 ProjToUV(in float2 f2ProjSpaceXY)
{
    return float2(0.5, 0.5) + float2(0.5, -0.5) * f2ProjSpaceXY;
}

float2 UVToProj(in float2 f2UV)
{
    return float2(-1.0, 1.0) + float2(2.0, -2.0) * f2UV;
}

float GetCamSpaceZ(in float2 ScreenSpaceUV)
{
    return g_tex2DCamSpaceZ.SampleLevel(samLinearClamp, ScreenSpaceUV, 0);
}

struct SScreenSizeQuadVSOutput
{
    float4 m_f4Pos : SV_Position;
    float2 m_f2PosPS : PosPS; // Position in projection space [-1,1]x[-1,1]
};

SScreenSizeQuadVSOutput GenerateScreenSizeQuadVS(in uint VertexId : SV_VertexID)
{
    float4 MinMaxUV = float4(-1, -1, 1, 1);
    
    SScreenSizeQuadVSOutput Verts[4] = 
    {
        {float4(MinMaxUV.xy, 1.0, 1.0), MinMaxUV.xy}, 
        {float4(MinMaxUV.xw, 1.0, 1.0), MinMaxUV.xw},
        {float4(MinMaxUV.zy, 1.0, 1.0), MinMaxUV.zy},
        {float4(MinMaxUV.zw, 1.0, 1.0), MinMaxUV.zw}
    };

    return Verts[VertexId];
}

float ReconstructCameraSpaceZPS(SScreenSizeQuadVSOutput In) : SV_Target
{
    float fDepth = g_tex2DDepthBuffer.Load( uint3(In.m_f4Pos.xy,0) );
    float fCamSpaceZ = g_CameraAttribs.mProj[3][2]/(fDepth - g_CameraAttribs.mProj[2][2]);
    return fCamSpaceZ;
};

technique11 ReconstructCameraSpaceZ
{
    pass
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepthTest, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, ReconstructCameraSpaceZPS() ) );
    }
}

// This function computes entry point of the epipolar line given its exit point
//                  
//    g_LightAttribs.f4LightScreenPos
//       *
//        \
//         \  f2EntryPoint
//        __\/___
//       |   \   |
//       |    \  |
//       |_____\_|
//           | |
//           | f2ExitPoint
//           |
//        Exit boundary
float2 GetEpipolarLineEntryPoint(float2 f2ExitPoint)
{
    float2 f2EntryPoint;

    //if( all( abs(g_LightAttribs.f4LightScreenPos.xy) < 1 ) )
    if( g_LightAttribs.bIsLightOnScreen )
    {
        // If light source is inside the screen its location is entry point for each epipolar line
        f2EntryPoint = g_LightAttribs.f4LightScreenPos.xy;
    }
    else
    {
        // If light source is outside the screen, we need to compute intersection of the ray with
        // the screen boundaries
        
        // Compute direction from the light source to the ray exit point:
        float2 f2RayDir = f2ExitPoint.xy - g_LightAttribs.f4LightScreenPos.xy;
        float fDistToExitBoundary = length(f2RayDir);
        f2RayDir /= fDistToExitBoundary;
        // Compute signed distances along the ray from the light position to all four boundaries
        // The distances are computed as follows using vector instructions:
        // float fDistToLeftBoundary   = abs(f2RayDir.x) > 1e-5 ? (-1 - g_LightAttribs.f4LightScreenPos.x) / f2RayDir.x : -FLT_MAX;
        // float fDistToBottomBoundary = abs(f2RayDir.y) > 1e-5 ? (-1 - g_LightAttribs.f4LightScreenPos.y) / f2RayDir.y : -FLT_MAX;
        // float fDistToRightBoundary  = abs(f2RayDir.x) > 1e-5 ? ( 1 - g_LightAttribs.f4LightScreenPos.x) / f2RayDir.x : -FLT_MAX;
        // float fDistToTopBoundary    = abs(f2RayDir.y) > 1e-5 ? ( 1 - g_LightAttribs.f4LightScreenPos.y) / f2RayDir.y : -FLT_MAX;
        bool4 b4IsCorrectIntersectionFlag = abs(f2RayDir.xyxy) > 1e-5;
        float4 f4DistToBoundaries = (float4(-1,-1,1,1) - g_LightAttribs.f4LightScreenPos.xyxy) / (f2RayDir.xyxy + !b4IsCorrectIntersectionFlag);
        // Addition of !b4IsCorrectIntersectionFlag is required to prevent divison by zero
        // Note that such incorrect lanes will be masked out anyway

        // We now need to find first intersection BEFORE the intersection with the exit boundary
        // This means that we need to find maximum intersection distance which is less than fDistToBoundary
        // We thus need to skip all boundaries, distance to which is greater than the distance to exit boundary
        // Using -FLT_MAX as the distance to these boundaries will result in skipping them:
        b4IsCorrectIntersectionFlag = b4IsCorrectIntersectionFlag && ( f4DistToBoundaries < (fDistToExitBoundary - 1e-4) );
        f4DistToBoundaries = b4IsCorrectIntersectionFlag * f4DistToBoundaries + 
                            !b4IsCorrectIntersectionFlag * float4(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);

        float fFirstIntersecDist = 0;
        fFirstIntersecDist = max(fFirstIntersecDist, f4DistToBoundaries.x);
        fFirstIntersecDist = max(fFirstIntersecDist, f4DistToBoundaries.y);
        fFirstIntersecDist = max(fFirstIntersecDist, f4DistToBoundaries.z);
        fFirstIntersecDist = max(fFirstIntersecDist, f4DistToBoundaries.w);
        
        // The code above is equivalent to the following lines:
        // fFirstIntersecDist = fDistToLeftBoundary   < fDistToBoundary-1e-4 ? max(fFirstIntersecDist, fDistToLeftBoundary)   : fFirstIntersecDist;
        // fFirstIntersecDist = fDistToBottomBoundary < fDistToBoundary-1e-4 ? max(fFirstIntersecDist, fDistToBottomBoundary) : fFirstIntersecDist;
        // fFirstIntersecDist = fDistToRightBoundary  < fDistToBoundary-1e-4 ? max(fFirstIntersecDist, fDistToRightBoundary)  : fFirstIntersecDist;
        // fFirstIntersecDist = fDistToTopBoundary    < fDistToBoundary-1e-4 ? max(fFirstIntersecDist, fDistToTopBoundary)    : fFirstIntersecDist;

        // Now we can compute entry point:
        f2EntryPoint = g_LightAttribs.f4LightScreenPos.xy + f2RayDir * fFirstIntersecDist;

        // For invalid rays, coordinates are outside [-1,1]x[-1,1] area
        // and such rays will be discarded
        //
        //       g_LightAttribs.f4LightScreenPos
        //             *
        //              \|
        //               \-f2EntryPoint
        //               |\
        //               | \  f2ExitPoint 
        //               |__\/___
        //               |       |
        //               |       |
        //               |_______|
        //
    }

    return f2EntryPoint;
}

void GenerateCoordinateTexturePS(SScreenSizeQuadVSOutput In, 
                                 out float2 f2XY : SV_Target0,
                                 out float fCamSpaceZ : SV_Target1
#if REFINE_INSCTR_INTEGRAL
                                 , out float3 f3WorldSpaceXYZ : SV_Target2
#endif
                                 )

{
    float2 f2UV = ProjToUV(In.m_f2PosPS);

    // Note that due to the rasterization rules, UV coordinates are biased by 0.5 texel size.
    //
    //      0.5     1.5     2.5     3.5
    //   |   X   |   X   |   X   |   X   |     ....       
    //   0       1       2       3       4   f2UV * g_PPAttribs.m_f2CoordinateTexDim
    //   X - locations where rasterization happens
    //
    // We need remove this offset:
    float fSamplePosOnEpipolarLine = f2UV.x - 0.5f / g_PPAttribs.m_f2CoordinateTexDim.x;
    // Clamp to [0,1] to fix fp32 precision issues
    float fEpipolarSlice = saturate(f2UV.y - 0.5f / g_PPAttribs.m_f2CoordinateTexDim.y);

    // fSamplePosOnEpipolarLine is now in the range [0, 1 - 1/g_PPAttribs.m_f2CoordinateTexDim.x]
    // We need to rescale it to be in [0, 1]
    fSamplePosOnEpipolarLine *= g_PPAttribs.m_f2CoordinateTexDim.x / (g_PPAttribs.m_f2CoordinateTexDim.x-1);
    fSamplePosOnEpipolarLine = saturate(fSamplePosOnEpipolarLine);

    // fEpipolarSlice lies in the range [0, 1 - 1/g_PPAttribs.m_f2CoordinateTexDim.y]
    // 0 defines location in exacatly left top corner, 1 - 1/g_PPAttribs.m_f2CoordinateTexDim.y defines
    // position on the top boundary next to the top left corner
    uint uiBoundary = clamp(floor( fEpipolarSlice * 4 ), 0, 3);
    float fPosOnBoundary = frac( fEpipolarSlice * 4 );

    //             <------
    //   +1   0,1___________0.75
    //          |     3     |
    //        | |           | A
    //        | |0         2| |
    //        V |           | |
    //   -1     |_____1_____|
    //       0.25  ------>  0.5
    //
    //         -1          +1
    //
    float fBoundaryCoord = -1 + 2*fPosOnBoundary;
    //                                   Left             Bttom           Right              Top   
    float4 f4BoundaryXCoord = float4(             -1, fBoundaryCoord,              1, -fBoundaryCoord);
    float4 f4BoundaryYCoord = float4(-fBoundaryCoord,             -1, fBoundaryCoord,               1);
    bool4 b4BoundaryFlags = bool4(uiBoundary.xxxx == uint4(0,1,2,3));
    // Select the right coordinates for the boundary
    float2 f2ExitPoint = float2(dot(f4BoundaryXCoord, b4BoundaryFlags), dot(f4BoundaryYCoord, b4BoundaryFlags));
    float2 f2EntryPoint = GetEpipolarLineEntryPoint(f2ExitPoint);
    if( any(abs(f2EntryPoint) > 1+1e-4) )
    {
        // Discard invalid rays
        // Such rays will not be marked in the stencil and as a result will always be skipped
        discard;
    }

#if OPTIMIZE_SAMPLE_LOCATIONS
    // Compute length of the epipolar line in screen pixels:
    float fEpipolarSliceScreenLen = length( (f2ExitPoint - f2EntryPoint) * g_PPAttribs.m_f2ScreenResolution.xy / 2 );
    // If epipolar line is too short, update epipolar line exit point to provide 1:1 texel to screen pixel correspondence:
    f2ExitPoint = f2EntryPoint + (f2ExitPoint - f2EntryPoint) * max(g_PPAttribs.m_f2CoordinateTexDim.x / fEpipolarSliceScreenLen, 1);
#endif

    // Compute interpolated position between entry and exit points:
    f2XY = lerp(f2EntryPoint, f2ExitPoint, fSamplePosOnEpipolarLine);
    if( any(abs(f2XY) > 1+1e-4) )
    {
        // Discard pixels that fall behind the screen
        discard;
    }

    // Compute camera space z for current location
    fCamSpaceZ = GetCamSpaceZ( ProjToUV(f2XY) );
#if REFINE_INSCTR_INTEGRAL
    f3WorldSpaceXYZ = ProjSpaceXYZToWorldSpace( float3(f2XY, fCamSpaceZ) );
#endif
};


technique11 GenerateCoordinateTexture
{
    pass
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        // Increase stencil value for all valid rays
        SetDepthStencilState( DSS_NoDepthTestIncrStencil, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, GenerateCoordinateTexturePS() ) );
    }
}



float2 RenderSliceUVDirInShadowMapTexturePS(SScreenSizeQuadVSOutput In) : SV_Target
{
    // Load location of the first sample in the slice (after the light source, which is 0-th sample)
    uint uiSliceInd = In.m_f4Pos.x;
    float2 f2FirstSampleInSliceLocationPS = g_tex2DCoordinates.Load( uint3(1, uiSliceInd, 0) );
    if( any( abs(f2FirstSampleInSliceLocationPS) > 1 + 1e-4 ) )
        return float2(-10000, -10000);

    // Reproject the sample location from camera projection space to light space
    float4 f4FirstSampleInSlicePosInLightProjSpace = mul( float4(f2FirstSampleInSliceLocationPS, 0, 1), g_LightAttribs.mCameraProjToLightProjSpace);
    f4FirstSampleInSlicePosInLightProjSpace /= f4FirstSampleInSlicePosInLightProjSpace.w;
    float2 f2FirstSampleInSliceUVInShadowMap = ProjToUV( f4FirstSampleInSlicePosInLightProjSpace.xy );

    // Compute direction from the camera pos in light space to the sample pos
    float2 f2SliceDir = f2FirstSampleInSliceUVInShadowMap - g_LightAttribs.f4CameraUVAndDepthInShadowMap.xy;
    f2SliceDir /= max(abs(f2SliceDir.x), abs(f2SliceDir.y));
    
    return f2SliceDir;
}

technique11 RenderSliceUVDirInShadowMapTexture
{
    pass p0
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        // Only interpolation samples will not be discarded and increase the stencil value
        SetDepthStencilState( DSS_NoDepthTest, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, RenderSliceUVDirInShadowMapTexturePS() ) );
    }
}

// Note that min/max shadow map does not contain finest resolution level
// The first level it contains corresponds to step == 2
float2 InitializeMinMaxShadowMapPS(SScreenSizeQuadVSOutput In) : SV_Target
{
    uint uiSliceInd = In.m_f4Pos.y;
    // Load slice direction in shadow map
    float2 f2SliceDir = g_tex2DSliceUVDirInShadowMap.Load( uint3(uiSliceInd,0,0) );
    // Calculate current sample position on the ray
    float2 f2CurrUV = g_LightAttribs.f4CameraUVAndDepthInShadowMap.xy + f2SliceDir * floor(In.m_f4Pos.x) * 2.f * g_PPAttribs.m_f2ShadowMapTexelSize;
    
    // Gather 8 depths which will be used for PCF filtering for this sample and its immediate neighbor 
    // along the epipolar slice
    // Note that if the sample is located outside the shadow map, Gather() will return 0 as 
    // specified by the samLinearBorder0. As a result volumes outside the shadow map will always be lit
    float4 f4Depths = 1 - g_tex2DLightSpaceDepthMap.Gather(samLinearBorder0, f2CurrUV);
    // Shift UV to the next sample along the epipolar slice:
    f2CurrUV += f2SliceDir * g_PPAttribs.m_f2ShadowMapTexelSize;
    float4 f4NeighbDepths = 1 - g_tex2DLightSpaceDepthMap.Gather(samLinearBorder0, f2CurrUV);

    float4 f4MinDepth = min(f4Depths, f4NeighbDepths);
    f4MinDepth.xy = min(f4MinDepth.xy, f4MinDepth.zw);
    f4MinDepth.x = min(f4MinDepth.x, f4MinDepth.y);

    float4 f4MaxDepth = max(f4Depths, f4NeighbDepths);
    f4MaxDepth.xy = max(f4MaxDepth.xy, f4MaxDepth.zw);
    f4MaxDepth.x = max(f4MaxDepth.x, f4MaxDepth.y);

    return float2(f4MinDepth.x, f4MaxDepth.x);
}

// 1D min max mip map is arranged as follows:
//
//    g_MiscParams.ui4SrcDstMinMaxLevelOffset.x
//     |
//     |      g_MiscParams.ui4SrcDstMinMaxLevelOffset.z
//     |_______|____ __
//     |       |    |  |
//     |       |    |  |
//     |       |    |  |
//     |       |    |  |
//     |_______|____|__|
//     |<----->|<-->|
//         |     |
//         |    uiMinMaxShadowMapResolution/
//      uiMinMaxShadowMapResolution/2
//                         
float2 ComputeMinMaxShadowMapLevelPS(SScreenSizeQuadVSOutput In) : SV_Target
{
    uint2 uiDstSampleInd = uint2(In.m_f4Pos.xy);
    uint2 uiSrcSample0Ind = uint2(g_MiscParams.ui4SrcDstMinMaxLevelOffset.x + (uiDstSampleInd.x - g_MiscParams.ui4SrcDstMinMaxLevelOffset.z)*2, uiDstSampleInd.y);
    uint2 uiSrcSample1Ind = uiSrcSample0Ind + uint2(1,0);
    float2 f2MinMaxDepth0 = 1 - g_tex2DMinMaxLightSpaceDepth.Load( uint3(uiSrcSample0Ind,0) );
    float2 f2MinMaxDepth1 = 1 - g_tex2DMinMaxLightSpaceDepth.Load( uint3(uiSrcSample1Ind,0) );
    float2 f2MinMaxDepth;
    f2MinMaxDepth.x = min(f2MinMaxDepth0.x, f2MinMaxDepth1.x);
    f2MinMaxDepth.y = max(f2MinMaxDepth0.y, f2MinMaxDepth1.y);
    return f2MinMaxDepth;
}

technique11 BuildMinMaxMipMap
{
    pass PInitializeMinMaxShadowMap
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        // Only interpolation samples will not be discarded and increase the stencil value
        SetDepthStencilState( DSS_NoDepthTest, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, InitializeMinMaxShadowMapPS() ) );
    }

    pass PComputeMinMaxShadowMapLevel
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        // Only interpolation samples will not be discarded and increase the stencil value
        SetDepthStencilState( DSS_NoDepthTest, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, ComputeMinMaxShadowMapLevelPS() ) );
    }
}


void MarkRayMarchingSamplesInStencilPS(SScreenSizeQuadVSOutput In)
{
    uint2 ui2InterpolationSources = g_tex2DInterpolationSource.Load( uint3(In.m_f4Pos.xy,0) );
    // Ray marching samples are interpolated from themselves, so it is easy to detect them:
    if( ui2InterpolationSources.x != ui2InterpolationSources.y )
          discard;
}

technique11 MarkRayMarchingSamplesInStencil
{
    pass
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        // Only interpolation samples will not be discarded and increase the stencil value
        SetDepthStencilState( DSS_NoDepth_StEqual_IncrStencil, 1 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, MarkRayMarchingSamplesInStencilPS() ) );
    }
}

float3 ProjSpaceXYToWorldSpace(in float2 f2PosPS)
{
    // We can sample camera space z texture using bilinear filtering
    float fCamSpaceZ = g_tex2DCamSpaceZ.SampleLevel(samLinearClamp, ProjToUV(f2PosPS), 0);
    return ProjSpaceXYZToWorldSpace(float3(f2PosPS, fCamSpaceZ));
}

float3 WorldSpaceToShadowMapUV(in float3 f3PosWS)
{
    float4 f4LightProjSpacePos = mul( float4(f3PosWS, 1), g_LightAttribs.mWorldToLightProjSpace );
    f4LightProjSpacePos /= f4LightProjSpacePos.w;
    float3 f3UVAndDepthInLightSpace;
    f3UVAndDepthInLightSpace.xy = ProjToUV( f4LightProjSpacePos.xy );
    // Applying depth bias results in light leaking through the opaque objects when looking directly
    // at the light source
    f3UVAndDepthInLightSpace.z = f4LightProjSpacePos.z;// * g_DepthBiasMultiplier;
    return f3UVAndDepthInLightSpace;
}

float3 CalculateCoarseInsctrIntergal(in float fStartDistFromCamera, 
                                     in float fEndDistFromCamera,
                                     in float3 f3EyeVec)
{
    // Perform coarse inscattering integral calculation using fixed number of steps
    float3 f3StartUVAndDepthInLightSpace = WorldSpaceToShadowMapUV(g_CameraAttribs.f4CameraPos.xyz + f3EyeVec * fStartDistFromCamera);
    float3 f3EndUVAndDepthInLightSpace = WorldSpaceToShadowMapUV(g_CameraAttribs.f4CameraPos.xyz + f3EyeVec * fEndDistFromCamera);
    f3StartUVAndDepthInLightSpace.z -= SHADOW_MAP_DEPTH_BIAS;
    f3EndUVAndDepthInLightSpace.z -= SHADOW_MAP_DEPTH_BIAS;

    static const float fShadowMapSteps = 4.f;
    float3 f3PrevInsctrIntegral = -exp( -fStartDistFromCamera * g_MediaParams.f4SummTotalBeta.rgb );
    float3 f3InsctrIntegralDelta = 0;
    [unroll]
    for(float fPos = 0.5/fShadowMapSteps; fPos < 1; fPos += 1.f/fShadowMapSteps )
    {
        float3 f3CurrUVAndDepthInLightSpace = lerp(f3StartUVAndDepthInLightSpace, f3EndUVAndDepthInLightSpace, fPos);
        float fCurrDepthInLightSpace = max(f3CurrUVAndDepthInLightSpace.z - 1e-4, 1e-7);
        float fIsInLight = 1 - g_tex2DLightSpaceDepthMap.SampleCmpLevelZero( samComparison, f3CurrUVAndDepthInLightSpace.xy, fCurrDepthInLightSpace ).x;
        float fCurrDist = lerp(fStartDistFromCamera, fEndDistFromCamera, fPos + 0.5f/fShadowMapSteps);
        float3 f3InsctrIntegral = -exp( -fCurrDist * g_MediaParams.f4SummTotalBeta.rgb );
        float3 dSctrIntegral = (f3InsctrIntegral - f3PrevInsctrIntegral) * fIsInLight;
#if STAINED_GLASS
            float4 SGWColor = g_tex2DStainedGlassColorDepth.SampleLevel( samLinearClamp, f3CurrUVAndDepthInLightSpace.xy, 0).rgba;
            float3 f3LightColorInCurrPoint = ((SGWColor.a < fCurrDepthInLightSpace) ? float3(1,1,1) : SGWColor.rgb*3);
            dSctrIntegral *= f3LightColorInCurrPoint;
#endif
        f3InsctrIntegralDelta += dSctrIntegral;
        f3PrevInsctrIntegral = f3InsctrIntegral;
    }
    f3InsctrIntegralDelta /= g_MediaParams.f4SummTotalBeta.rgb;

    return f3InsctrIntegralDelta;
}

float3 InterpolateIrradiancePS(SScreenSizeQuadVSOutput In) : SV_Target
{
    uint uiSampleInd = In.m_f4Pos.x;
    uint uiSliceInd = In.m_f4Pos.y;
    // Get interpolation sources
    uint2 ui2InterpolationSources = g_tex2DInterpolationSource.Load( uint3(uiSampleInd, uiSliceInd, 0) );
    float fInterpolationPos = float(uiSampleInd - ui2InterpolationSources.x) / float( max(ui2InterpolationSources.y - ui2InterpolationSources.x,1) );

    float3 f3Src0 = g_tex2DInitialInsctrIrradiance.Load( uint3(ui2InterpolationSources.x, uiSliceInd, 0) );
    float3 f3Src1 = g_tex2DInitialInsctrIrradiance.Load( uint3(ui2InterpolationSources.y, uiSliceInd, 0));

#if REFINE_INSCTR_INTEGRAL
    float3 f3SamplePosWS = g_tex2DEpipolarSampleWorldPos.Load( uint3(uiSampleInd, uiSliceInd,0) ).xyz;
    float3 f3SrcSample0PosWS = g_tex2DEpipolarSampleWorldPos.Load( uint3(ui2InterpolationSources.x, uiSliceInd,0) ).xyz;
    float3 f3SrcSample1PosWS = g_tex2DEpipolarSampleWorldPos.Load( uint3(ui2InterpolationSources.y, uiSliceInd,0) ).xyz;
    

    float3 f3EyeVec, f3Src0EyeVec, f3Src1EyeVec;
    float fRayLength, fSrc0RayLength, fSrc1RayLength;
    GetTracingAttribs(f3SamplePosWS, f3EyeVec, fRayLength);
    GetTracingAttribs(f3SrcSample0PosWS, f3Src0EyeVec, fSrc0RayLength);
    GetTracingAttribs(f3SrcSample1PosWS, f3Src1EyeVec, fSrc1RayLength);

    // Refine source integrals
    f3Src0 = max( f3Src0 + CalculateCoarseInsctrIntergal(fSrc0RayLength, fRayLength, f3EyeVec), 0 ); 
    f3Src1 = max( f3Src1 + CalculateCoarseInsctrIntergal(fSrc1RayLength, fRayLength, f3EyeVec), 0 ); 
#endif

    // Ray marching samples are interpolated from themselves
    return lerp(f3Src0, f3Src1, fInterpolationPos);
}

technique11 InterpolateIrradiance
{
    pass
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepthTest, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, InterpolateIrradiancePS() ) );
    }
}


float3 PerformBilateralInterpolation(in float2 f2BilinearWeights,
                                     in float2 f2LeftBottomSrcTexelUV,
                                     in float4 f4SrcLocationsCamSpaceZ,
                                     in float  fFilteringLocationCamSpaceZ,
                                     in Texture2D<float3> tex2DSrcTexture,
                                     in float2 f2SrcTexDim,
                                     in SamplerState Sampler)
{
    // Initialize bilateral weights with bilinear:
    float4 f4BilateralWeights = 
        //Offset:       (x=0,y=1)            (x=1,y=1)             (x=1,y=0)               (x=0,y=0)
        float4(1 - f2BilinearWeights.x, f2BilinearWeights.x,   f2BilinearWeights.x, 1 - f2BilinearWeights.x) * 
        float4(    f2BilinearWeights.y, f2BilinearWeights.y, 1-f2BilinearWeights.y, 1 - f2BilinearWeights.y);

    // Compute depth weights in a way that if the difference is less than the threshold, the weight is 1 and
    // the weights fade out to 0 as the difference becomes larger than the threshold:
    float4 f4DepthWeights = saturate( g_PPAttribs.m_fRefinementThreshold / max( abs(fFilteringLocationCamSpaceZ-f4SrcLocationsCamSpaceZ), g_PPAttribs.m_fRefinementThreshold ) );
    // Note that if the sample is located outside the [-1,1]x[-1,1] area, the sample is invalid and fCurrCamSpaceZ == fInvalidCoordinate
    // Depth weight computed for such sample will be zero
    f4DepthWeights = pow(f4DepthWeights, 4);
    // Multiply bilinear weights with the depth weights:
    f4BilateralWeights *= f4DepthWeights;
    // Compute summ weight
    float fTotalWeight = dot(f4BilateralWeights, float4(1,1,1,1));
    
    float3 f3ScatteredLight = 0;
    [branch]
    if( g_PPAttribs.m_bCorrectScatteringAtDepthBreaks && fTotalWeight < 1e-2 )
    {
        // Discarded pixels will keep 0 value in stencil and will be later
        // processed to correct scattering
        discard;
    }
    else
    {
        // Normalize weights
        f4BilateralWeights /= fTotalWeight;

        // We now need to compute the following weighted summ:
        //f3ScatteredLight = 
        //    f4BilateralWeights.x * tex2DSrcTexture.SampleLevel(samPoint, f2ScatteredColorIJ, 0, int2(0,1)) +
        //    f4BilateralWeights.y * tex2DSrcTexture.SampleLevel(samPoint, f2ScatteredColorIJ, 0, int2(1,1)) +
        //    f4BilateralWeights.z * tex2DSrcTexture.SampleLevel(samPoint, f2ScatteredColorIJ, 0, int2(1,0)) +
        //    f4BilateralWeights.w * tex2DSrcTexture.SampleLevel(samPoint, f2ScatteredColorIJ, 0, int2(0,0));

        // We will use hardware to perform bilinear filtering and get these values using just two bilinear fetches:

        // Offset:                  (x=1,y=0)                (x=1,y=0)               (x=0,y=0)
        float fRow0UOffset = f4BilateralWeights.z / max(f4BilateralWeights.z + f4BilateralWeights.w, 0.001);
        fRow0UOffset /= f2SrcTexDim.x;
        float3 f3Row0WeightedCol = 
            (f4BilateralWeights.z + f4BilateralWeights.w) * 
                tex2DSrcTexture.SampleLevel(Sampler, f2LeftBottomSrcTexelUV + float2(fRow0UOffset, 0), 0, int2(0,0));

        // Offset:                  (x=1,y=1)                 (x=0,y=1)              (x=1,y=1)
        float fRow1UOffset = f4BilateralWeights.y / max(f4BilateralWeights.x + f4BilateralWeights.y, 0.001);
        fRow1UOffset /= f2SrcTexDim.x;
        float3 f3Row1WeightedCol = 
            (f4BilateralWeights.x + f4BilateralWeights.y) * 
                tex2DSrcTexture.SampleLevel(Sampler, f2LeftBottomSrcTexelUV + float2(fRow1UOffset, 0 ), 0, int2(0,1));
        
        f3ScatteredLight = f3Row0WeightedCol + f3Row1WeightedCol;
    }

    return f3ScatteredLight;
}


float3 UnwarpEpipolarInsctrImage( SScreenSizeQuadVSOutput In, in float fCamSpaceZ )
{
    // Compute direction of the ray going from the light through the pixel
    float2 f2RayDir = normalize( In.m_f2PosPS - g_LightAttribs.f4LightScreenPos.xy );

    // Find, which boundary the ray intersects. For this, we will 
    // find which two of four half spaces the f2RayDir belongs to
    // Each of four half spaces is produced by the line connecting one of four
    // screen corners and the current pixel:
    //    ________________        _______'________           ________________           
    //   |'            . '|      |      '         |         |                |          
    //   | '       . '    |      |     '          |      .  |                |          
    //   |  '  . '        |      |    '           |        '|.        hs1    |          
    //   |   *.           |      |   *     hs0    |         |  '*.           |          
    //   |  '   ' .       |      |  '             |         |      ' .       |          
    //   | '        ' .   |      | '              |         |          ' .   |          
    //   |'____________ '_|      |'_______________|         | ____________ '_.          
    //                           '                                             '
    //                           ________________  .        '________________  
    //                           |             . '|         |'               | 
    //                           |   hs2   . '    |         | '              | 
    //                           |     . '        |         |  '             | 
    //                           | . *            |         |   *            | 
    //                         . '                |         |    '           | 
    //                           |                |         | hs3 '          | 
    //                           |________________|         |______'_________| 
    //                                                              '
    // The equations for the half spaces are the following:
    //bool hs0 = (In.m_f2PosPS.x - (-1)) * f2RayDir.y < f2RayDir.x * (In.m_f2PosPS.y - (-1));
    //bool hs1 = (In.m_f2PosPS.x -  (1)) * f2RayDir.y < f2RayDir.x * (In.m_f2PosPS.y - (-1));
    //bool hs2 = (In.m_f2PosPS.x -  (1)) * f2RayDir.y < f2RayDir.x * (In.m_f2PosPS.y -  (1));
    //bool hs3 = (In.m_f2PosPS.x - (-1)) * f2RayDir.y < f2RayDir.x * (In.m_f2PosPS.y -  (1));
    float4 f4HalfSpaceEquationTerms = (In.m_f2PosPS.xxyy - float4(-1,1,-1,1)) * f2RayDir.yyxx;
    bool4 b4HalfSpaceFlags = f4HalfSpaceEquationTerms.xyyx < f4HalfSpaceEquationTerms.zzww;

    // Now compute mask indicating which of four sectors the f2RayDir belongs to and consiquently
    // which border the ray intersects:
    //    ________________ 
    //   |'            . '|         0 : hs3 && !hs0
    //   | '   3   . '    |         1 : hs0 && !hs1
    //   |  '  . '        |         2 : hs1 && !hs2
    //   |0  *.       2   |         3 : hs2 && !hs3
    //   |  '   ' .       |
    //   | '   1    ' .   |
    //   |'____________ '_|
    //
    bool4 b4SectorFlags = b4HalfSpaceFlags.wxyz && !b4HalfSpaceFlags.xyzw;
    // Note that b4SectorFlags now contains true (1) for the exit boundary and false (0) for 3 other

    // Compute distances to boundaries according to following lines:
    //float fDistToLeftBoundary   = abs(f2RayDir.x) > 1e-5 ? ( -1 - g_LightAttribs.f4LightScreenPos.x) / f2RayDir.x : -FLT_MAX;
    //float fDistToBottomBoundary = abs(f2RayDir.y) > 1e-5 ? ( -1 - g_LightAttribs.f4LightScreenPos.y) / f2RayDir.y : -FLT_MAX;
    //float fDistToRightBoundary  = abs(f2RayDir.x) > 1e-5 ? (  1 - g_LightAttribs.f4LightScreenPos.x) / f2RayDir.x : -FLT_MAX;
    //float fDistToTopBoundary    = abs(f2RayDir.y) > 1e-5 ? (  1 - g_LightAttribs.f4LightScreenPos.y) / f2RayDir.y : -FLT_MAX;
    float4 f4DistToBoundaries = ( float4(-1,-1, 1,1) - g_LightAttribs.f4LightScreenPos.xyxy ) / (f2RayDir.xyxy + float4( abs(f2RayDir.xyxy)<1e-6 ) );
    // Select distance to the exit boundary:
    float fDistToExitBoundary = dot( b4SectorFlags, f4DistToBoundaries );
    // Compute exit point on the boundary:
    float2 f2ExitPoint = g_LightAttribs.f4LightScreenPos.xy + f2RayDir * fDistToExitBoundary;
    float2 f2EntryPoint = GetEpipolarLineEntryPoint(f2ExitPoint);
    // Compute epipolar slice for each boundary:
    //if( LeftBoundary )
    //    fEpipolarSlice = 0.0 + (0.5 - 0.5*LeftBoudaryIntersecPoint.y)/4;
    //else if( BottomBoundary )
    //    fEpipolarSlice = 0.25 + (0.5*BottomBoudaryIntersecPoint.x + 0.5)/4;
    //else if( RightBoundary )
    //    fEpipolarSlice = 0.5 + (0.5 + 0.5*RightBoudaryIntersecPoint.y)/4;
    //else if( TopBoundary )
    //    fEpipolarSlice = 0.75 + (0.5 - 0.5*TopBoudaryIntersecPoint.x)/4;
    float4 f4EpipolarSlice = float4(0, 0.25, 0.5, 0.75) + (0.5 + float4(-0.5, +0.5, +0.5, -0.5)*f2ExitPoint.yxyx)/4.0;
    // Select the right value:
    float fEpipolarSlice = dot(b4SectorFlags, f4EpipolarSlice);

#if OPTIMIZE_SAMPLE_LOCATIONS
    // Compute length of the epipolar line in screen pixels:
    float fEpipolarSliceScreenLen = length( (f2ExitPoint - f2EntryPoint) * g_PPAttribs.m_f2ScreenResolution.xy / 2 );
    // If epipolar line is too short, update epipolar line exit point to provide 1:1 texel to screen pixel correspondence:
    f2ExitPoint = f2EntryPoint + (f2ExitPoint - f2EntryPoint) * max(g_PPAttribs.m_f2CoordinateTexDim.x / fEpipolarSliceScreenLen, 1);
#endif

    float2 f2EpipolarSliceDir = f2ExitPoint - f2EntryPoint.xy;
    float fEpipolarSliceLen = length(f2EpipolarSliceDir);
    f2EpipolarSliceDir /= max(fEpipolarSliceLen, 1e-6);

    // Project current pixel onto the epipolar slice
    float fProj = dot((In.m_f2PosPS - f2EntryPoint.xy), f2EpipolarSliceDir) / fEpipolarSliceLen;

    // We need to manually perform bilateral filtering of the scattered radiance texture to
    // eliminate artifacts at depth discontinuities
    float2 f2ScatteredColorUV = float2(fProj, fEpipolarSlice);
    float2 f2ScatteredColorTexDim;
    g_tex2DScatteredColor.GetDimensions(f2ScatteredColorTexDim.x, f2ScatteredColorTexDim.y);
    // Offset by 0.5 is essential, because texel centers have UV coordinates that are offset by half the texel size
    float2 f2ScatteredColorUVScaled = f2ScatteredColorUV.xy * f2ScatteredColorTexDim.xy - float2(0.5, 0.5);
    float2 f2ScatteredColorIJ = floor(f2ScatteredColorUVScaled);
    // Get bilinear filtering weights
    float2 f2BilinearWeights = f2ScatteredColorUVScaled - f2ScatteredColorIJ;
    // Get texture coordinates of the left bottom source texel. Again, offset by 0.5 is essential
    // to align with texel center
    f2ScatteredColorIJ = (f2ScatteredColorIJ + float2(0.5, 0.5)) / f2ScatteredColorTexDim.xy;
    
    // Gather 4 camera space z values
    // Note that we need to bias f2ScatteredColorIJ by 0.5 texel size to get the required values
    //   _______ _______
    //  |       |       |
    //  |       |       |
    //  |_______X_______|  X gather location
    //  |       |       |
    //  |   *   |       |  * f2ScatteredColorIJ
    //  |_______|_______|
    //  |<----->|
    //     1/f2ScatteredColorTexDim.x
    float4 f4SrcLocationsCamSpaceZ = g_tex2DEpipolarCamSpaceZ.Gather(samLinearClamp, f2ScatteredColorIJ + float2(0.5, 0.5) / f2ScatteredColorTexDim.xy);
    // The values in f4SrcLocationsCamSpaceZ are arranged as follows:
    // f4SrcLocationsCamSpaceZ.x == g_tex2DEpipolarCamSpaceZ.SampleLevel(samPointClamp, f2ScatteredColorIJ, 0, int2(0,1))
    // f4SrcLocationsCamSpaceZ.y == g_tex2DEpipolarCamSpaceZ.SampleLevel(samPointClamp, f2ScatteredColorIJ, 0, int2(1,1))
    // f4SrcLocationsCamSpaceZ.z == g_tex2DEpipolarCamSpaceZ.SampleLevel(samPointClamp, f2ScatteredColorIJ, 0, int2(1,0))
    // f4SrcLocationsCamSpaceZ.w == g_tex2DEpipolarCamSpaceZ.SampleLevel(samPointClamp, f2ScatteredColorIJ, 0, int2(0,0))

    return PerformBilateralInterpolation(f2BilinearWeights, f2ScatteredColorIJ, f4SrcLocationsCamSpaceZ, fCamSpaceZ, g_tex2DScatteredColor, f2ScatteredColorTexDim, samLinearUClampVWrap);
}

float3 GetExtinction(float in_Dist)
{
    float3 vExtinction;
    // Use analytical expression for extinction (see "Rendering Outdoor Light Scattering in Real Time" by 
    // Hoffman and Preetham, p.27 and p.51) 
    vExtinction = exp( -(g_MediaParams.f4TotalRayleighBeta.rgb +  g_MediaParams.f4TotalMieBeta.rgb) * in_Dist );
    return vExtinction;
}

float3 GetAttenuatedBackgroundColor(SScreenSizeQuadVSOutput In, in float fDistToCamera )
{
    float3 f3BackgroundColor = 0;
    [branch]
    if( !g_PPAttribs.m_bShowLightingOnly )
    {
        f3BackgroundColor = g_tex2DColorBuffer.Load(int3(In.m_f4Pos.xy,0)).rgb;
        float3 f3Extinction = GetExtinction(fDistToCamera);
        f3BackgroundColor *= f3Extinction.rgb;
    }
    return f3BackgroundColor;
}

float3 GetAttenuatedBackgroundColor(SScreenSizeQuadVSOutput In)
{
    float3 f3WorldSpacePos = ProjSpaceXYToWorldSpace(In.m_f2PosPS.xy);
    float fDistToCamera = length(f3WorldSpacePos - g_CameraAttribs.f4CameraPos.xyz);
    return GetAttenuatedBackgroundColor(In, fDistToCamera);
}

float3 ApplyInscatteredRadiancePS(SScreenSizeQuadVSOutput In) : SV_Target
{
    float fCamSpaceZ = GetCamSpaceZ( ProjToUV(In.m_f2PosPS) );
    float3 f3InsctrIntegral = UnwarpEpipolarInsctrImage(In, fCamSpaceZ);
    
    float3 f3ReconstructedPosWS = ProjSpaceXYZToWorldSpace(float3(In.m_f2PosPS.xy, fCamSpaceZ));
    float3 f3EyeVector = f3ReconstructedPosWS.xyz - g_CameraAttribs.f4CameraPos.xyz;
    float fDistToCamera = length(f3EyeVector);
    f3EyeVector /= fDistToCamera;
    float3 f3InsctrColor = ApplyPhaseFunction(f3InsctrIntegral, f3EyeVector);

    float3 f3BackgroundColor = GetAttenuatedBackgroundColor(In, fDistToCamera);
    return f3BackgroundColor + f3InsctrColor;
}

float3 UnwarpEpipolarInsctrImagePS( SScreenSizeQuadVSOutput In ) : SV_Target
{
    // Get camera space z of the current screen pixel
    float fCamSpaceZ = GetCamSpaceZ( ProjToUV(In.m_f2PosPS) );
    return UnwarpEpipolarInsctrImage( In, fCamSpaceZ );
}

technique11 ApplyInscatteredRadiance
{
    pass PAttenuateBackgroundAndApplyInsctr
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepthTestIncrStencil, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, UnwarpEpipolarInsctrImagePS() ) );
    }

    pass PUnwarpInsctr
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepthTestIncrStencil, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, ApplyInscatteredRadiancePS() ) );
    }
}

float3 UpscaleInscatteredRadiancePS(SScreenSizeQuadVSOutput In) : SV_Target
{
    float2 f2UV = ProjToUV(In.m_f2PosPS);

    // We need to manually perform bilateral filtering of the downscaled scattered radiance texture to
    // eliminate artifacts at depth discontinuities
    float2 f2DownscaledInsctrTexDim;
    g_tex2DDownscaledInsctrRadiance.GetDimensions(f2DownscaledInsctrTexDim.x, f2DownscaledInsctrTexDim.y);
    // Offset by 0.5 is essential, because texel centers have UV coordinates that are offset by half the texel size
    float2 f2UVScaled = f2UV.xy * f2DownscaledInsctrTexDim.xy - float2(0.5, 0.5);
    float2 f2LeftBottomSrcTexelUV = floor(f2UVScaled);
    // Get bilinear filtering weights
    float2 f2BilinearWeights = f2UVScaled - f2LeftBottomSrcTexelUV;
    // Get texture coordinates of the left bottom source texel. Again, offset by 0.5 is essential
    // to align with texel center
    f2LeftBottomSrcTexelUV = (f2LeftBottomSrcTexelUV + float2(0.5, 0.5)) / f2DownscaledInsctrTexDim.xy;

    // Load camera space Z values corresponding to locations of the source texels in g_tex2DDownscaledInsctrRadiance texture
    // We must arrange the data in the same manner as Gather() does:
    float4 f4SrcLocationsCamSpaceZ;
    f4SrcLocationsCamSpaceZ.x = GetCamSpaceZ( f2LeftBottomSrcTexelUV + float2(0,1) / f2DownscaledInsctrTexDim.xy );
    f4SrcLocationsCamSpaceZ.y = GetCamSpaceZ( f2LeftBottomSrcTexelUV + float2(1,1) / f2DownscaledInsctrTexDim.xy );
    f4SrcLocationsCamSpaceZ.z = GetCamSpaceZ( f2LeftBottomSrcTexelUV + float2(1,0) / f2DownscaledInsctrTexDim.xy );
    f4SrcLocationsCamSpaceZ.w = GetCamSpaceZ( f2LeftBottomSrcTexelUV + float2(0,0) / f2DownscaledInsctrTexDim.xy );

    // Get camera space z of the current screen pixel
    float fCamSpaceZ = GetCamSpaceZ( f2UV );

    float3 f3InsctrIntegral = PerformBilateralInterpolation(f2BilinearWeights, f2LeftBottomSrcTexelUV, f4SrcLocationsCamSpaceZ, fCamSpaceZ, g_tex2DDownscaledInsctrRadiance, f2DownscaledInsctrTexDim, samLinearClamp);
    
    float3 f3ReconstructedPosWS = ProjSpaceXYZToWorldSpace( float3(In.m_f2PosPS.xy,fCamSpaceZ) );
    float3 f3EyeVector = f3ReconstructedPosWS.xyz - g_CameraAttribs.f4CameraPos.xyz;
    float fDistToCamera = length(f3EyeVector);
    f3EyeVector /= fDistToCamera;
    float3 f3ScatteredLight = ApplyPhaseFunction(f3InsctrIntegral, f3EyeVector);

    float3 f3BackgroundColor = GetAttenuatedBackgroundColor(In, fDistToCamera);

    return f3BackgroundColor + f3ScatteredLight;
}

technique11 UpscaleInscatteredRadiance
{
    pass
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepthTestIncrStencil, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, UpscaleInscatteredRadiancePS() ) );
    }
}


struct PassThroughVS_Output
{
    uint uiVertexID : VERTEX_ID;
};

PassThroughVS_Output PassThroughVS(uint VertexID : SV_VertexID)
{
    PassThroughVS_Output Out = {VertexID};
    return Out;
}



struct SRenderSamplePositionsGS_Output
{
    float4 f4PosPS : SV_Position;
    float3 f3Color : COLOR;
    float2 f2PosXY : XY;
    float4 f4QuadCenterAndSize : QUAD_CENTER_SIZE;
};
[maxvertexcount(4)]
void RenderSamplePositionsGS(point PassThroughVS_Output In[1], 
                             inout TriangleStream<SRenderSamplePositionsGS_Output> triStream )
{
    uint2 CoordTexDim;
    g_tex2DCoordinates.GetDimensions(CoordTexDim.x, CoordTexDim.y);
    uint2 TexelIJ = uint2( In[0].uiVertexID%CoordTexDim.x, In[0].uiVertexID/CoordTexDim.x );
    float2 f2QuadCenterPos = g_tex2DCoordinates.Load(int3(TexelIJ,0));

    uint2 ui2InterpolationSources = g_tex2DInterpolationSource.Load( uint3(TexelIJ,0) );
    bool bIsInterpolation = ui2InterpolationSources.x != ui2InterpolationSources.y;

    float2 f2QuadSize = (bIsInterpolation ? 1.f : 4.f) / g_PPAttribs.m_f2ScreenResolution.xy;
    float4 MinMaxUV = float4(f2QuadCenterPos.x-f2QuadSize.x, f2QuadCenterPos.y - f2QuadSize.y, f2QuadCenterPos.x+f2QuadSize.x, f2QuadCenterPos.y + f2QuadSize.y);
    
    float3 f3Color = bIsInterpolation ? float3(0.5,0,0) : float3(1,0,0);
    float4 Verts[4] = 
    {
        float4(MinMaxUV.xy, 1.0, 1.0), 
        float4(MinMaxUV.xw, 1.0, 1.0),
        float4(MinMaxUV.zy, 1.0, 1.0),
        float4(MinMaxUV.zw, 1.0, 1.0)
    };

    for(int i=0; i<4; i++)
    {
        SRenderSamplePositionsGS_Output Out;
        Out.f4PosPS = Verts[i];
        Out.f2PosXY = Out.f4PosPS.xy;
        Out.f3Color = f3Color;
        Out.f4QuadCenterAndSize = float4(f2QuadCenterPos, f2QuadSize);
        triStream.Append( Out );
    }
}

float4 RenderSampleLocationsPS(SRenderSamplePositionsGS_Output In) : SV_Target
{
    return float4(In.f3Color, 1 - pow( length( (In.f2PosXY - In.f4QuadCenterAndSize.xy) / In.f4QuadCenterAndSize.zw),4) );
}

BlendState OverBS
{
    BlendEnable[0] = TRUE;
    RenderTargetWriteMask[0] = 0x0F;
    BlendOp = ADD;
    SrcBlend = SRC_ALPHA;
    DestBlend = INV_SRC_ALPHA;
    SrcBlendAlpha = ZERO;
    DestBlendAlpha = INV_SRC_ALPHA;
};

technique11 RenderSampleLocations
{
    pass
    {
        SetBlendState( OverBS, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepthTest, 0 );

        SetVertexShader( CompileShader(vs_5_0, PassThroughVS() ) );
        SetGeometryShader( CompileShader(gs_4_0, RenderSamplePositionsGS() ) );
        SetPixelShader( CompileShader(ps_5_0, RenderSampleLocationsPS() ) );
    }
}

// This function calculates inscattered light integral over the ray from the camera to 
// the specified world space position using ray marching
float3 CalculateInscattering( in float2 f2RayMarchingSampleLocation,
                              in uniform const bool bApplyPhaseFunction = false,
                              in uniform const bool bUse1DMinMaxMipMap = false,
                              uint uiEpipolarSliceInd = 0 )
{
    float3 f3RayEndPos = ProjSpaceXYToWorldSpace(f2RayMarchingSampleLocation);

    float3 f3EyeVector;
    float fTraceLength;
    GetTracingAttribs(f3RayEndPos, f3EyeVector, fTraceLength);

    // If tracing distance is very short, we can fall into an inifinte loop due to
    // 0 length step and crash the driver. Return from function in this case
    if( fTraceLength < g_PPAttribs.m_fMaxTracingDistance * 0.0001)
        return float3(0,0,0);
    
    // We trace the ray not in the world space, but in the light projection space

    // Get start and end positions of the ray in the light projection space
    float3 f3StartUVAndDepthInLightSpace = g_LightAttribs.f4CameraUVAndDepthInShadowMap.xyz;
    f3StartUVAndDepthInLightSpace.z -= SHADOW_MAP_DEPTH_BIAS;
    // Compute shadow map UV coordiantes of the ray end point and its depth in the light space
    float3 f3EndUVAndDepthInLightSpace = WorldSpaceToShadowMapUV(f3RayEndPos);
    f3EndUVAndDepthInLightSpace.z -= SHADOW_MAP_DEPTH_BIAS;

    // Calculate normalized trace direction in the light projection space and its length
    float3 f3ShadowMapTraceDir = f3EndUVAndDepthInLightSpace - f3StartUVAndDepthInLightSpace;
    // If the ray is directed exactly at the light source, trace length will be zero
    // Clamp to a very small positive value to avoid division by zero
    float fTraceLenInShadowMapUVSpace = max( length( f3ShadowMapTraceDir.xy ), 1e-6);
    f3ShadowMapTraceDir /= fTraceLenInShadowMapUVSpace;
    
    float fShadowMapUVStepLen = 0;
    if( bUse1DMinMaxMipMap )
    {
        // Get UV direction for this slice
        float2 f2SliceDir = g_tex2DSliceUVDirInShadowMap.Load( uint3(uiEpipolarSliceInd,0,0) );
        // Scale with the shadow map texel size
        fShadowMapUVStepLen = length(f2SliceDir*g_PPAttribs.m_f2ShadowMapTexelSize);
    }
    else
    {
        //Calculate length of the trace step in light projection space
        fShadowMapUVStepLen = g_PPAttribs.m_f2ShadowMapTexelSize.x / max( abs(f3ShadowMapTraceDir.x), abs(f3ShadowMapTraceDir.y) );
        // Take into account maximum number of steps specified by the g_MiscParams.fMaxStepsAlongRay
        fShadowMapUVStepLen = max(fTraceLenInShadowMapUVSpace/g_MiscParams.fMaxStepsAlongRay, fShadowMapUVStepLen);
    }
    
    // Calcualte ray step length in world space
    float fRayStepLengthWS = fTraceLength * (fShadowMapUVStepLen / fTraceLenInShadowMapUVSpace);
    // Assure that step length is not 0 so that we will not fall into an infinite loop and
    // will not crash the driver
    //fRayStepLengthWS = max(fRayStepLengthWS, g_PPAttribs.m_fMaxTracingDistance * 1e-5);

    // Scale trace direction in light projection space to calculate the final step
    float3 f3ShadowMapUVAndDepthStep = f3ShadowMapTraceDir.xyz * fShadowMapUVStepLen;

    float3 f3InScatteringIntegral = 0;
    float3 f3PrevInsctrIntegralValue = -1; // -exp( -0 * g_MediaParams.f4SummTotalBeta.rgb );
    // March the ray
    float fTotalMarchedDistance = 0;
    float3 f3CurrShadowMapUVAndDepthInLightSpace = f3StartUVAndDepthInLightSpace;

    // The following variables are used only if 1D min map optimization is enabled
    uint uiMinLevel = max( log2( (fTraceLenInShadowMapUVSpace/fShadowMapUVStepLen) / g_MiscParams.fMaxStepsAlongRay), 0 );
    uint uiCurrSamplePos = 0;
    uint uiCurrTreeLevel = 0;
    // Note that min/max shadow map does not contain finest resolution level
    // The first level it contains corresponds to step == 2
    int iLevelDataOffset = -int(g_PPAttribs.m_uiMinMaxShadowMapResolution);
    float fStep = 1.f;
    float fMaxShadowMapStep = g_PPAttribs.m_uiMaxShadowMapStep;

    [loop]
    while( fTotalMarchedDistance < fTraceLength )
    {
        // Clamp depth to a very small positive value to not let the shadow rays get clipped at the
        // shadow map far clipping plane
        float fCurrDepthInLightSpace = max(f3CurrShadowMapUVAndDepthInLightSpace.z, 1e-7);
        float IsInLight = 0;
        if( bUse1DMinMaxMipMap )
        {
            // If the step is smaller than the maximum allowed and the sample
            // is located at the appropriate position, advance to the next coarser level
            if( fStep < fMaxShadowMapStep && ((uiCurrSamplePos & ((2<<uiCurrTreeLevel)-1)) == 0) )
            {
                iLevelDataOffset += g_PPAttribs.m_uiMinMaxShadowMapResolution >> uiCurrTreeLevel;
                uiCurrTreeLevel++;
                fStep *= 2.f;
            }
            
            while(uiCurrTreeLevel > uiMinLevel)
            {
                // Compute minimum and maximum light space depths at the ends of the current ray section
                float2 f2MinMaxDepthOnRaySection;
                float fNextLightSpaceDepth = f3CurrShadowMapUVAndDepthInLightSpace.z + f3ShadowMapUVAndDepthStep.z * fStep;
                f2MinMaxDepthOnRaySection.x = min(f3CurrShadowMapUVAndDepthInLightSpace.z, fNextLightSpaceDepth);
                f2MinMaxDepthOnRaySection.y = max(f3CurrShadowMapUVAndDepthInLightSpace.z, fNextLightSpaceDepth);
                f2MinMaxDepthOnRaySection = max(f2MinMaxDepthOnRaySection, 1e-7);
                // Load 1D min/max depths
                float2 f2CurrMinMaxDepth = 1 - g_tex2DMinMaxLightSpaceDepth.Load( uint3( (uiCurrSamplePos>>uiCurrTreeLevel) + iLevelDataOffset, uiEpipolarSliceInd, 0) );    
#if !STAINED_GLASS
                IsInLight = (f2CurrMinMaxDepth.y <= f2MinMaxDepthOnRaySection.x) ? 1.f : 0.f;
#endif
                bool bIsInShadow = f2CurrMinMaxDepth.x > f2MinMaxDepthOnRaySection.y;
                if( IsInLight || bIsInShadow )
                    // If the ray section is fully lit or shadow, we can break the loop
                    break;

                // If the ray section is neither fully lit, nor shadowed, we have to go to the finer level
                uiCurrTreeLevel--;
                iLevelDataOffset -= g_PPAttribs.m_uiMinMaxShadowMapResolution >> uiCurrTreeLevel;
                fStep /= 2.f;
            };

            // If we are at the finest level, sample the shadow map with PCF
            [branch]
            if( uiCurrTreeLevel <= uiMinLevel )
            {
                IsInLight = 1 - g_tex2DLightSpaceDepthMap.SampleCmpLevelZero( samComparison, f3CurrShadowMapUVAndDepthInLightSpace.xy, fCurrDepthInLightSpace  ).x;
            }
        }
        else
        {
            IsInLight = 1 - g_tex2DLightSpaceDepthMap.SampleCmpLevelZero( samComparison, f3CurrShadowMapUVAndDepthInLightSpace.xy, fCurrDepthInLightSpace ).x;
        }

        float3 LightColorInCurrPoint;
        LightColorInCurrPoint = g_LightAttribs.f4SunColorAndIntensityAtGround.rgb;

#if STAINED_GLASS
        float4 SGWColor = g_tex2DStainedGlassColorDepth.SampleLevel( samLinearClamp, f3CurrShadowMapUVAndDepthInLightSpace.xy, 0).rgba;
        LightColorInCurrPoint.rgb *= ((SGWColor.a < fCurrDepthInLightSpace) ? float3(1,1,1) : SGWColor.rgb*3);
#endif

        fTotalMarchedDistance += fRayStepLengthWS * fStep;
        f3CurrShadowMapUVAndDepthInLightSpace += f3ShadowMapUVAndDepthStep * fStep;
        uiCurrSamplePos += 1 << uiCurrTreeLevel; // int -> float conversions are slow

        float fIntegrationDist = min(fTotalMarchedDistance, fTraceLength);

        // Calculate inscattering integral from the camera to the current point analytically:
        float3 f3CurrInscatteringIntegralValue = -exp( -fIntegrationDist * g_MediaParams.f4SummTotalBeta.rgb );//ComputeInsctrLightIntegral(fIntegrationDist);

        float3 dScatteredLight;
        // dScatteredLight contains correct scattering light value with respect to extinction
        dScatteredLight.rgb = (f3CurrInscatteringIntegralValue.rgb - f3PrevInsctrIntegralValue.rgb) * IsInLight;
        dScatteredLight.rgb *= LightColorInCurrPoint;
        f3InScatteringIntegral.rgb += dScatteredLight.rgb;

        f3PrevInsctrIntegralValue.rgb = f3CurrInscatteringIntegralValue.rgb;
    }

    f3InScatteringIntegral = f3InScatteringIntegral / g_MediaParams.f4SummTotalBeta.rgb;

    if( bApplyPhaseFunction )
        return ApplyPhaseFunction(f3InScatteringIntegral, f3EyeVector);
    else
        return f3InScatteringIntegral;
}

float3 RayMarchMinMaxOptPS(SScreenSizeQuadVSOutput In) : SV_TARGET
{
    uint2 ui2SamplePosSliceInd = uint2(In.m_f4Pos.xy);
    float2 f2SampleLocation = g_tex2DCoordinates.Load( uint3(ui2SamplePosSliceInd, 0) );

    [branch]
    if( any(abs(f2SampleLocation) > 1+1e-3) )
        return 0;

    return CalculateInscattering(f2SampleLocation, 
                                 false, // Do not apply phase function
                                 true,  // Use min/max optimization
                                 ui2SamplePosSliceInd.y);
}

float3 RayMarchPS(SScreenSizeQuadVSOutput In) : SV_TARGET
{
    float2 f2SampleLocation = g_tex2DCoordinates.Load( uint3(In.m_f4Pos.xy, 0) );

    [branch]
    if( any(abs(f2SampleLocation) > 1+1e-3) )
        return 0;

    return CalculateInscattering(f2SampleLocation, 
                                 false, // Do not apply phase function
                                 false, // Do not use min/max optimization
                                 0 // Ignored
                                 );
}

technique10 DoRayMarch
{
    pass P0
    {
        // Skip all samples which are not marked in the stencil as ray marching
        SetDepthStencilState( DSS_NoDepth_StEqual_IncrStencil, 2 );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );

        SetVertexShader( CompileShader( vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader(NULL);
        SetPixelShader( CompileShader( ps_5_0, RayMarchPS() ) );
    }

    pass P1
    {
        // Skip all samples which are not marked in the stencil as ray marching
        SetDepthStencilState( DSS_NoDepth_StEqual_IncrStencil, 2 );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );

        SetVertexShader( CompileShader( vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader(NULL);
        SetPixelShader( CompileShader( ps_5_0, RayMarchMinMaxOptPS() ) );
    }
}

float3 FixInscatteredRadiancePS(SScreenSizeQuadVSOutput In) : SV_Target
{
    if( g_PPAttribs.m_bShowDepthBreaks )
        return float3(0,0,1e+3);

    return CalculateInscattering(In.m_f2PosPS.xy, 
                                 false, // Do not apply phase function
                                 false, // We cannot use min/max optimization at depth breaks
                                 0 // Ignored
                                 );
}

float3 FixAndApplyInscatteredRadiancePS(SScreenSizeQuadVSOutput In) : SV_Target
{
    if( g_PPAttribs.m_bShowDepthBreaks )
        return float3(0,1,0);

    float3 f3BackgroundColor = GetAttenuatedBackgroundColor(In);
    
    float3 f3InsctrColor = 
        CalculateInscattering(In.m_f2PosPS.xy, 
                              true, // Apply phase function
                              false, // We cannot use min/max optimization at depth breaks
                              0 // Ignored
                              );

    return f3BackgroundColor + f3InsctrColor.rgb;
}

technique11 FixInscatteredRadiance
{
    pass PAttenuateBackground
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepth_StEqual_IncrStencil, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, FixInscatteredRadiancePS() ) );
    }

    pass PRenderScatteringOnly
    {
        SetBlendState( NoBlending, float4( 0.0f, 0.0f, 0.0f, 0.0f ), 0xFFFFFFFF );
        SetRasterizerState( RS_SolidFill_NoCull );
        SetDepthStencilState( DSS_NoDepth_StEqual_IncrStencil, 0 );

        SetVertexShader( CompileShader(vs_5_0, GenerateScreenSizeQuadVS() ) );
        SetGeometryShader( NULL );
        SetPixelShader( CompileShader(ps_5_0, FixAndApplyInscatteredRadiancePS() ) );
    }
}
