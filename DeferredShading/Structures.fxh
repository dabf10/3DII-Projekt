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

#ifndef _STRCUTURES_FXH_
#define _STRCUTURES_FXH_

#ifdef __cplusplus

#   define float2 D3DXVECTOR2
#   define float3 D3DXVECTOR3
#   define float4 D3DXVECTOR4
#   define uint UINT

#else

#   define BOOL bool // Do not use bool, because sizeof(bool)==1 !

#endif

struct SLightAttribs
{
    float4 f4DirectionOnSun;               ///< Direction on sun
    float4 f4SunColorAndIntensityAtGround; ///< Sun color
    float4 f4AmbientLight;                 ///< Ambient light
    float4 f4CameraUVAndDepthInShadowMap;
    float4 f4LightScreenPos;
    
    BOOL bIsLightOnScreen;
    float3 f3Dummy;

#ifdef __cplusplus
    D3DXMATRIX mLightViewT;
    D3DXMATRIX mLightProjT;
    D3DXMATRIX mWorldToLightProjSpaceT;
    D3DXMATRIX mCameraProjToLightProjSpaceT;
#else
    matrix mLightView;
    matrix mLightProj;
    matrix mWorldToLightProjSpace;
    matrix mCameraProjToLightProjSpace;
#endif
};

struct SCameraAttribs
{
    float4 f4CameraPos;            ///< Camera world position
#ifdef __cplusplus
    D3DXMATRIX mViewT;
    D3DXMATRIX mProjT;
    D3DXMATRIX mViewProjInvT;
#else
    matrix mView;
    matrix mProj;
    matrix mViewProjInv;
#endif
};

struct SPostProcessingAttribs
{
    // 0
    uint m_uiNumEpipolarSlices;
    uint m_uiMaxSamplesInSlice;
    uint m_uiInitialSampleStepInSlice;
    uint m_uiEpipoleSamplingDensityFactor;

    // 4
    float m_fRefinementThreshold;
    float m_fDownscaleFactor;
    // do not use bool, because sizeof(bool)==1 and as a result bool variables
    // will be incorrectly mapped on GPU constant buffer
    BOOL m_bShowSampling; 
    BOOL m_bCorrectScatteringAtDepthBreaks; 

    // 8
    BOOL m_bShowDepthBreaks; 
    BOOL m_bStainedGlass;
    BOOL m_bShowLightingOnly;
    BOOL m_bOptimizeSampleLocations;

    // 12
    BOOL m_bMinMaxShadowMapOptimization;
    float m_fDistanceScaler;
    float m_fMaxTracingDistance;
    uint m_uiMaxShadowMapStep;

    // 16
    float2 m_f2ShadowMapTexelSize;
    uint m_uiShadowMapResolution;
    uint m_uiMinMaxShadowMapResolution;

    // 20
    float2 m_f2CoordinateTexDim;
    float2 m_f2ScreenResolution;

    // 24
    BOOL m_bRefineInsctrIntegral;
    float3 f3Dummy;

    // 28
    float4 m_f4RayleighBeta;
    float4 m_f4MieBeta;

#ifdef __cplusplus
    SPostProcessingAttribs() : 
        m_uiNumEpipolarSlices(512),
        m_uiMaxSamplesInSlice(256),
        m_uiInitialSampleStepInSlice(16),
        // Note that sampling near the epipole is very cheap since only a few steps
        // required to perform ray marching
        m_uiEpipoleSamplingDensityFactor(4),
        m_fRefinementThreshold(0.2f),
        m_fDownscaleFactor(4.f),
        m_bShowSampling(FALSE),
        m_bCorrectScatteringAtDepthBreaks(TRUE),
        m_bShowDepthBreaks(FALSE),
        m_bStainedGlass(FALSE),
        m_bShowLightingOnly(FALSE),
        m_bOptimizeSampleLocations(TRUE),
        m_bMinMaxShadowMapOptimization(TRUE),
        m_fDistanceScaler(1.f),
        m_fMaxTracingDistance(20.f),
        m_uiMaxShadowMapStep(16),
        m_f2ShadowMapTexelSize(0,0),
        m_uiMinMaxShadowMapResolution(0),
        m_f2CoordinateTexDim( static_cast<float>(m_uiMaxSamplesInSlice), static_cast<float>(m_uiNumEpipolarSlices) ),
        m_f2ScreenResolution(1024, 768),
        m_bRefineInsctrIntegral(FALSE),
        m_f4RayleighBeta( 5.8e-6f, 13.5e-6f, 33.1e-6f, 0.f ),
        m_f4MieBeta(2.0e-5f, 2.0e-5f, 2.0e-5f, 0.f)
    {}
#endif
};

struct SParticipatingMediaScatteringParams
{
    // Atmospheric light scattering constants
    float4 f4TotalRayleighBeta;
    float4 f4AngularRayleighBeta;
    float4 f4TotalMieBeta;
    float4 f4AngularMieBeta;
    float4 f4HG_g; // = float4(1 - HG_g*HG_g, 1 + HG_g*HG_g, -2*HG_g, 1.0);
    float4 f4SummTotalBeta;

#define INSCATTERING_MULTIPLIER 27.f/3.f   ///< Light scattering constant - Inscattering multiplier    
};

struct SMiscDynamicParams
{
#ifdef __cplusplus
    uint ui4SrcMinMaxLevelXOffset;
    uint ui4SrcMinMaxLevelYOffset;
    uint ui4DstMinMaxLevelXOffset;
    uint ui4DstMinMaxLevelYOffset;
#else
    uint4 ui4SrcDstMinMaxLevelOffset;
#endif
    float fMaxStepsAlongRay;   // Maximum number of steps during ray tracing
    float3 f3Dummy; // Constant buffers must be 16-byte aligned
};

#endif //_STRCUTURES_FXH_