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

#include "LightSctrPostProcess.h"
#include <atlcomcli.h>
#include <cassert>
#include <stdio.h>

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef V
#define V(x)           { hr = (x); assert( SUCCEEDED(hr) ); }
#endif
#ifndef V_RETURN
#define V_RETURN(x)    { hr = (x); assert( SUCCEEDED(hr) ); if( FAILED(hr) ) { return hr; } }
#endif

CLightSctrPostProcess :: CLightSctrPostProcess() : 
    m_uiSampleRefinementCSThreadGroupSize(0),
    // Using small group size is inefficient because a lot of SIMD lanes become idle
    m_uiSampleRefinementCSMinimumThreadGroupSize(128),// Must be greater than 32
    m_fTurbidity(1.02f),
    m_strEffectPath( L"LightScattering.fx" )
{
    ComputeScatteringCoefficients();
}

CLightSctrPostProcess :: ~CLightSctrPostProcess()
{

}

HRESULT CLightSctrPostProcess :: OnCreateDevice(ID3D11Device* in_pd3dDevice, 
                                                ID3D11DeviceContext *in_pd3dDeviceContext)
{
    

    HRESULT hr;


    // Create depth stencil states

    // Disable depth testing
    D3D11_DEPTH_STENCIL_DESC DisableDepthTestDSDesc;
    ZeroMemory(&DisableDepthTestDSDesc, sizeof(DisableDepthTestDSDesc));
    DisableDepthTestDSDesc.DepthEnable = FALSE;
    DisableDepthTestDSDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    V_RETURN( in_pd3dDevice->CreateDepthStencilState(  &DisableDepthTestDSDesc, &m_pDisableDepthTestDS) );
    
    // Disable depth testing and always increment stencil value
    // This depth stencil state is used to mark samples which will undergo further processing
    // Pixel shader discards pixels which should not be further processed, thus keeping the
    // stencil value untouched
    // For instance, pixel shader performing epipolar coordinates generation discards all 
    // sampes, whoose coordinates are outside the screen [-1,1]x[-1,1] area
    D3D11_DEPTH_STENCIL_DESC DisbaleDepthIncrStencilDSSDesc = DisableDepthTestDSDesc;
    DisbaleDepthIncrStencilDSSDesc.StencilEnable = TRUE;
    DisbaleDepthIncrStencilDSSDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    DisbaleDepthIncrStencilDSSDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
    DisbaleDepthIncrStencilDSSDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    DisbaleDepthIncrStencilDSSDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    DisbaleDepthIncrStencilDSSDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
    DisbaleDepthIncrStencilDSSDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
    DisbaleDepthIncrStencilDSSDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
    DisbaleDepthIncrStencilDSSDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    DisbaleDepthIncrStencilDSSDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
    DisbaleDepthIncrStencilDSSDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
    V_RETURN( in_pd3dDevice->CreateDepthStencilState(  &DisbaleDepthIncrStencilDSSDesc, &m_pDisableDepthTestIncrStencilDS) );


    // Disable depth testing, stencil testing function equal, increment stencil
    // This state is used to process only these pixels that were marked at the previous pass
    // All pixels whith different stencil value are discarded from further processing as well
    // as some pixels can also be discarded during the draw call
    // For instance, pixel shader marking ray marching samples processes only these pixels which are inside
    // the screen. It also discards all but these samples which are interpolated from themselves
    D3D11_DEPTH_STENCIL_DESC DisbaleDepthStencilEqualIncrStencilDSSDesc = DisbaleDepthIncrStencilDSSDesc;
    DisbaleDepthStencilEqualIncrStencilDSSDesc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    DisbaleDepthStencilEqualIncrStencilDSSDesc.BackFace.StencilFunc = D3D11_COMPARISON_EQUAL;
    V_RETURN( in_pd3dDevice->CreateDepthStencilState(  &DisbaleDepthStencilEqualIncrStencilDSSDesc, &m_pNoDepth_StEqual_IncrStencilDS) );

    // Create rasterizer state
    D3D11_RASTERIZER_DESC SolidFillNoCullRSDesc;
    ZeroMemory(&SolidFillNoCullRSDesc, sizeof(SolidFillNoCullRSDesc));
    SolidFillNoCullRSDesc.FillMode = D3D11_FILL_SOLID;
    SolidFillNoCullRSDesc.CullMode = D3D11_CULL_NONE;
    V_RETURN( in_pd3dDevice->CreateRasterizerState( &SolidFillNoCullRSDesc, &m_pSolidFillNoCullRS) );

    // Create default blend state
    D3D11_BLEND_DESC DefaultBlendStateDesc;
    ZeroMemory(&DefaultBlendStateDesc, sizeof(DefaultBlendStateDesc));
    DefaultBlendStateDesc.IndependentBlendEnable = FALSE;
    for(int i=0; i< _countof(DefaultBlendStateDesc.RenderTarget); i++)
        DefaultBlendStateDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    V_RETURN( in_pd3dDevice->CreateBlendState( &DefaultBlendStateDesc, &m_pDefaultBS) );


    // Create samplers

    D3D11_SAMPLER_DESC SamLinearBorder0Desc = 
    {
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_TEXTURE_ADDRESS_BORDER,
        D3D11_TEXTURE_ADDRESS_BORDER,
        D3D11_TEXTURE_ADDRESS_BORDER,
        0, //FLOAT MipLODBias;
        0, //UINT MaxAnisotropy;
        D3D11_COMPARISON_NEVER, // D3D11_COMPARISON_FUNC ComparisonFunc;
        {0.f, 0.f, 0.f, 0.f}, //FLOAT BorderColor[ 4 ];
        +FLT_MAX, //FLOAT MinLOD;
        -FLT_MAX //FLOAT MaxLOD;
    };
    V_RETURN( in_pd3dDevice->CreateSamplerState( &SamLinearBorder0Desc, &m_psamLinearBorder0) );

    D3D11_SAMPLER_DESC SamLinearClampDesc = 
    {
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        0, //FLOAT MipLODBias;
        0, //UINT MaxAnisotropy;
        D3D11_COMPARISON_NEVER, // D3D11_COMPARISON_FUNC ComparisonFunc;
        {0.f, 0.f, 0.f, 0.f}, //FLOAT BorderColor[ 4 ];
        +FLT_MAX, //FLOAT MinLOD;
        -FLT_MAX //FLOAT MaxLOD;
    };
    V_RETURN( in_pd3dDevice->CreateSamplerState( &SamLinearClampDesc, &m_psamLinearClamp) );

    D3D11_SAMPLER_DESC SamLinearUClampVWrapDesc = 
    {
        D3D11_FILTER_MIN_MAG_MIP_LINEAR,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        D3D11_TEXTURE_ADDRESS_WRAP,
        D3D11_TEXTURE_ADDRESS_CLAMP,
        0, //FLOAT MipLODBias;
        0, //UINT MaxAnisotropy;
        D3D11_COMPARISON_NEVER, // D3D11_COMPARISON_FUNC ComparisonFunc;
        {0.f, 0.f, 0.f, 0.f}, //FLOAT BorderColor[ 4 ];
        +FLT_MAX, //FLOAT MinLOD;
        -FLT_MAX //FLOAT MaxLOD;
    };
    V_RETURN( in_pd3dDevice->CreateSamplerState( &SamLinearUClampVWrapDesc, &m_psamLinearUClampVWrap) );

    D3D11_SAMPLER_DESC SamComparisonDesc = 
    {
        D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D11_TEXTURE_ADDRESS_BORDER,
        D3D11_TEXTURE_ADDRESS_BORDER,
        D3D11_TEXTURE_ADDRESS_BORDER,
        0, //FLOAT MipLODBias;
        0, //UINT MaxAnisotropy;
        D3D11_COMPARISON_GREATER, // D3D11_COMPARISON_FUNC ComparisonFunc;
        {0.f, 0.f, 0.f, 0.f}, //FLOAT BorderColor[ 4 ];
        +FLT_MAX, //FLOAT MinLOD;
        -FLT_MAX //FLOAT MaxLOD;
    };
    V_RETURN( in_pd3dDevice->CreateSamplerState( &SamComparisonDesc, &m_psamComparison) );


    
    // Create constant buffers

    D3D11_BUFFER_DESC CBDesc = 
    {
        sizeof(SCameraAttribs),
        D3D11_USAGE_DYNAMIC,
        D3D11_BIND_CONSTANT_BUFFER,
        D3D11_CPU_ACCESS_WRITE, //UINT CPUAccessFlags
        0, //UINT MiscFlags;
        0, //UINT StructureByteStride;
    };
    V_RETURN( in_pd3dDevice->CreateBuffer( &CBDesc, NULL, &m_pcbCameraAttribs) );

    CBDesc.ByteWidth = sizeof(SLightAttribs);
    V_RETURN( in_pd3dDevice->CreateBuffer( &CBDesc, NULL, &m_pcbLightAttribs) );

    CBDesc.ByteWidth = sizeof(SPostProcessingAttribs);
    V_RETURN( in_pd3dDevice->CreateBuffer( &CBDesc, NULL, &m_pcbPostProcessingAttribs) );

    CBDesc.ByteWidth = sizeof(SMiscDynamicParams);
    V_RETURN( in_pd3dDevice->CreateBuffer( &CBDesc, NULL, &m_pcbMiscParams) );

    CBDesc.ByteWidth = sizeof(SParticipatingMediaScatteringParams);
    CBDesc.Usage = D3D11_USAGE_DEFAULT;
    CBDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData = 
    {
        &m_MediaParams,
        0, // UINT SysMemPitch
        0  // UINT SysMemSlicePitch
    };
    V_RETURN( in_pd3dDevice->CreateBuffer( &CBDesc, &InitData, &m_pcbMediaAttribs) );

    CRenderTechnique GenerateScreenSizeQuadTech;
    GenerateScreenSizeQuadTech.SetDeviceAndContext( in_pd3dDevice, in_pd3dDeviceContext );
    V( GenerateScreenSizeQuadTech.CreateVertexShaderFromFile(m_strEffectPath, "GenerateScreenSizeQuadVS", NULL ) );
    m_pGenerateScreenSizeQuadVS = GenerateScreenSizeQuadTech.GetVS();

    return S_OK;
}

void CLightSctrPostProcess :: OnDestroyDevice()
{
    m_ptex2DCoordianteTextureSRV.Release();
    m_ptex2DCoordianteTextureRTV.Release();
    m_ptex2DEpipolarImageDSV.Release();
    m_ptex2DInterpolationSourcesSRV.Release();
    m_ptex2DInterpolationSourcesUAV.Release();
    m_ptex2DEpipolarCamSpaceZSRV.Release();
    m_ptex2DEpipolarCamSpaceZRTV.Release();
    m_ptex2DEpipolarSampleWorldPosSRV.Release();
    m_ptex2DEpipolarSampleWorldPosRTV.Release();
    m_ptex2DScatteredLightSRV.Release();
    m_ptex2DScatteredLightRTV.Release();
    m_ptex2DInitialScatteredLightSRV.Release();
    m_ptex2DInitialScatteredLightRTV.Release();
    m_ptex2DDownscaledScatteredLightSRV.Release();
    m_ptex2DDownscaledScatteredLightRTV.Release();
    m_ptex2DScreenSizeDSV.Release();
    m_ptex2DDownscaledDSV.Release();
    m_ptex2DCameraSpaceZRTV.Release();
    m_ptex2DCameraSpaceZSRV.Release();
    for(int i=0; i < _countof(m_ptex2DMinMaxShadowMapSRV); ++i)
        m_ptex2DMinMaxShadowMapSRV[i].Release();
    for(int i=0; i < _countof(m_ptex2DMinMaxShadowMapRTV); ++i)
        m_ptex2DMinMaxShadowMapRTV[i].Release();
    m_ptex2DSliceUVDirInShadowMapSRV.Release();
    m_ptex2DSliceUVDirInShadowMapRTV.Release();

    m_psamLinearClamp.Release();
    m_psamLinearBorder0.Release();
    m_psamLinearUClampVWrap.Release();
    m_psamComparison.Release();

    m_ReconstrCamSpaceZTech.Release();
    m_RendedCoordTexTech.Release();
    m_RefineSampleLocationsTech.Release();
    m_MarkRayMarchingSamplesInStencilTech.Release();
    m_RenderSliceUVDirInSMTech.Release();
    m_InitializeMinMaxShadowMapTech.Release();
    m_ComputeMinMaxSMLevelTech.Release();
    m_DoRayMarchTech[0].Release();
    m_DoRayMarchTech[1].Release();
    m_InterpolateIrradianceTech.Release();
    m_UnwarpEpipolarSctrImgTech[0].Release();
    m_UnwarpEpipolarSctrImgTech[1].Release();
    m_FixInsctrAtDepthBreaksTech[0].Release();
    m_FixInsctrAtDepthBreaksTech[1].Release();
    m_UpscaleInsctrdRadianceTech.Release();
    m_RenderSampleLocationsTech.Release();
    
    m_pGenerateScreenSizeQuadVS.Release();

    m_pDisableDepthTestDS.Release();
    m_pDisableDepthTestIncrStencilDS.Release();
    m_pNoDepth_StEqual_IncrStencilDS.Release();

    m_pSolidFillNoCullRS.Release();

    m_pDefaultBS.Release();

    m_pcbCameraAttribs.Release();
    m_pcbLightAttribs.Release();
    m_pcbPostProcessingAttribs.Release();
    m_pcbMediaAttribs.Release();
    m_pcbMiscParams.Release();
}

HRESULT CLightSctrPostProcess :: OnResizedSwapChain(ID3D11Device* pd3dDevice, UINT uiBackBufferWidth, UINT uiBackBufferHeight)
{
    m_uiBackBufferWidth = uiBackBufferWidth;
    m_uiBackBufferHeight = uiBackBufferHeight;
    D3D11_TEXTURE2D_DESC ScreenSizeDepthStencilTexDesc = 
    {
        uiBackBufferWidth,                  //UINT Width;
        uiBackBufferHeight,                 //UINT Height;
        1,                                  //UINT MipLevels;
        1,                                  //UINT ArraySize;
        DXGI_FORMAT_D24_UNORM_S8_UINT,      //DXGI_FORMAT Format;
        {1,0},                              //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                //D3D11_USAGE Usage;
        D3D11_BIND_DEPTH_STENCIL,           //UINT BindFlags;
        0,                                  //UINT CPUAccessFlags;
        0,                                  //UINT MiscFlags;
    };

    m_ptex2DScreenSizeDSV.Release();
    CComPtr<ID3D11Texture2D> ptex2DScreenSizeDepthStencil;
    // Create 2-D texture, shader resource and target view buffers on the device
    HRESULT hr;
    V_RETURN( pd3dDevice->CreateTexture2D( &ScreenSizeDepthStencilTexDesc, NULL, &ptex2DScreenSizeDepthStencil) );
    V_RETURN( pd3dDevice->CreateDepthStencilView( ptex2DScreenSizeDepthStencil, NULL, &m_ptex2DScreenSizeDSV)  );

    m_ptex2DDownscaledDSV.Release();
    m_ptex2DDownscaledScatteredLightSRV.Release();
    m_ptex2DDownscaledScatteredLightRTV.Release();

    m_ptex2DCameraSpaceZRTV.Release();
    m_ptex2DCameraSpaceZSRV.Release();
    D3D11_TEXTURE2D_DESC CamSpaceZTexDesc = ScreenSizeDepthStencilTexDesc;
    CamSpaceZTexDesc.Format = DXGI_FORMAT_R32_FLOAT;
    CamSpaceZTexDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    CComPtr<ID3D11Texture2D> ptex2DCamSpaceZ;
    V_RETURN( pd3dDevice->CreateTexture2D( &CamSpaceZTexDesc, NULL, &ptex2DCamSpaceZ) );
    V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DCamSpaceZ, NULL, &m_ptex2DCameraSpaceZSRV)  );
    V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DCamSpaceZ, NULL, &m_ptex2DCameraSpaceZRTV)  );

    return S_OK;
}

static void UnbindResources(ID3D11DeviceContext *pDeviceCtx)
{
    ID3D11ShaderResourceView *pDummySRVs[8]={NULL};
    ID3D11UnorderedAccessView *pDummyUAVs[8]={NULL};
    pDeviceCtx->PSSetShaderResources(0, _countof(pDummySRVs), pDummySRVs);
    pDeviceCtx->VSSetShaderResources(0, _countof(pDummySRVs), pDummySRVs);
    pDeviceCtx->GSSetShaderResources(0, _countof(pDummySRVs), pDummySRVs);
    pDeviceCtx->CSSetShaderResources(0, _countof(pDummySRVs), pDummySRVs);
    pDeviceCtx->CSSetUnorderedAccessViews(0, _countof(pDummyUAVs), pDummyUAVs, NULL);
}

static void RenderQuad(ID3D11DeviceContext *pd3dDeviceCtx, 
                       CRenderTechnique &State, 
                       int iWidth = 0, int iHeight = 0,
                       int iTopLeftX = 0, int iTopLeftY = 0)
{
    // Initialize the viewport
    if( !iWidth && !iHeight )
    {
        assert( iTopLeftX == 0 && iTopLeftY == 0 );
        CComPtr<ID3D11RenderTargetView> pRTV;
        pd3dDeviceCtx->OMGetRenderTargets(1, &pRTV, NULL);
        CComPtr<ID3D11Resource> pDstTex;
        pRTV->GetResource( &pDstTex );
        D3D11_TEXTURE2D_DESC DstTexDesc;
        CComQIPtr<ID3D11Texture2D>(pDstTex)->GetDesc( &DstTexDesc );
        iWidth = DstTexDesc.Width;
        iHeight = DstTexDesc.Height;
    }
    
    D3D11_VIEWPORT NewViewPort;
    NewViewPort.TopLeftX = static_cast<float>( iTopLeftX );
    NewViewPort.TopLeftY = static_cast<float>( iTopLeftY );
    NewViewPort.Width  = static_cast<float>( iWidth );
    NewViewPort.Height = static_cast<float>( iHeight );
    NewViewPort.MinDepth = 0;
    NewViewPort.MaxDepth = 1;
    // Set the viewport
    pd3dDeviceCtx->RSSetViewports(1, &NewViewPort);  

    UINT offset[1] = {0};
    UINT stride[1] = {0};
    ID3D11Buffer *ppBuffers[1] = {0};
    pd3dDeviceCtx->IASetVertexBuffers(0, 1, ppBuffers, stride, offset);
    // There is no input-layout object and the primitive topology is triangle strip
    pd3dDeviceCtx->IASetInputLayout(NULL);
    pd3dDeviceCtx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    State.Apply();
    // Draw 4 vertices (two triangles )
    pd3dDeviceCtx->Draw(4, 0);
    
    // Unbind resources
    UnbindResources( pd3dDeviceCtx );
}

static void UpdateConstantBuffer(ID3D11DeviceContext *pDeviceCtx, ID3D11Buffer *pCB, const void *pData, size_t DataSize)
{
    D3D11_MAPPED_SUBRESOURCE MappedData;
    pDeviceCtx->Map(pCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &MappedData);
    memcpy(MappedData.pData, pData, DataSize);
    pDeviceCtx->Unmap(pCB, 0);
}

void CLightSctrPostProcess :: ReconstructCameraSpaceZ(SFrameAttribs &FrameAttribs)
{
    if( !m_ReconstrCamSpaceZTech.IsValid() )
    {
        m_ReconstrCamSpaceZTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
        m_ReconstrCamSpaceZTech.CreatePixelShaderFromFile( m_strEffectPath, "ReconstructCameraSpaceZPS", NULL );
        m_ReconstrCamSpaceZTech.SetVS( m_pGenerateScreenSizeQuadVS );
        m_ReconstrCamSpaceZTech.SetDS( m_pDisableDepthTestDS );
        m_ReconstrCamSpaceZTech.SetRS( m_pSolidFillNoCullRS );
        m_ReconstrCamSpaceZTech.SetBS( m_pDefaultBS );
    }

    // Depth buffer is non-linear and cannot be interpolated directly
    // We have to reconstruct camera space z to be able to use bilinear filtering
    
    // Set render target first, because depth buffer is still bound on output and it must be unbound 
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, &m_ptex2DCameraSpaceZRTV.p, NULL);
    // Texture2D<float> g_tex2DCamSpaceZ : register( t0 );
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, 1, &FrameAttribs.ptex2DDepthBufferSRV);
    RenderQuad( FrameAttribs.pd3dDeviceContext, 
                m_ReconstrCamSpaceZTech,
                m_uiBackBufferWidth, m_uiBackBufferHeight );
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
}

void CLightSctrPostProcess :: RenderCoordinateTexture(SFrameAttribs &FrameAttribs)
{
    if( !m_RendedCoordTexTech.IsValid() )
    {
        m_RendedCoordTexTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
        D3D_SHADER_MACRO Macros[] =
        {
            {"OPTIMIZE_SAMPLE_LOCATIONS", m_PostProcessingAttribs.m_bOptimizeSampleLocations ? "1" : "0"},
            {"REFINE_INSCTR_INTEGRAL", m_PostProcessingAttribs.m_bRefineInsctrIntegral ? "1" : "0"},
            {NULL, NULL}
        };
        m_RendedCoordTexTech.CreatePixelShaderFromFile( m_strEffectPath, "GenerateCoordinateTexturePS", Macros );
        m_RendedCoordTexTech.SetVS( m_pGenerateScreenSizeQuadVS );
        m_RendedCoordTexTech.SetDS( m_pDisableDepthTestIncrStencilDS );
        m_RendedCoordTexTech.SetRS( m_pSolidFillNoCullRS );
        m_RendedCoordTexTech.SetBS( m_pDefaultBS );
    }
    // Coordinate texture is a texture with dimensions [Total Samples X Num Slices]
    // Texel t[i,j] contains projection-space screen cooridantes of the i-th sample in j-th epipolar slice
    ID3D11RenderTargetView *ppRTVs[] = {m_ptex2DCoordianteTextureRTV, m_ptex2DEpipolarCamSpaceZRTV, m_ptex2DEpipolarSampleWorldPosRTV};
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(_countof(ppRTVs), ppRTVs, m_ptex2DEpipolarImageDSV);

    static const float fInvalidCoordinate = -1e+10;
    float InvalidCoords[] = {fInvalidCoordinate, fInvalidCoordinate, fInvalidCoordinate, fInvalidCoordinate};
    // Clear both render targets with values that can't be correct projection space coordinates and camera space Z:
    FrameAttribs.pd3dDeviceContext->ClearRenderTargetView(m_ptex2DCoordianteTextureRTV, InvalidCoords);
    FrameAttribs.pd3dDeviceContext->ClearRenderTargetView(m_ptex2DEpipolarCamSpaceZRTV, InvalidCoords);
    // Clear depth stencil view. Since we use stencil part only, there is no need to clear depth
    // Set stencil value to 0
    FrameAttribs.pd3dDeviceContext->ClearDepthStencilView(m_ptex2DEpipolarImageDSV, D3D11_CLEAR_STENCIL, 1.0f, 0);

    // Texture2D<float> g_tex2DCamSpaceZ : register( t0 );
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, 1, &m_ptex2DCameraSpaceZSRV.p); 

    // Depth stencil state is configured to always increment stencil value. If coordinates are outside the screen,
    // the pixel shader discards the pixel and stencil value is left untouched. All such pixels will be skipped from
    // further processing
    RenderQuad( FrameAttribs.pd3dDeviceContext, m_RendedCoordTexTech,
                m_PostProcessingAttribs.m_uiMaxSamplesInSlice, m_PostProcessingAttribs.m_uiNumEpipolarSlices );
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
}

void CLightSctrPostProcess :: RefineSampleLocations(SFrameAttribs &FrameAttribs)
{
    if( !m_RefineSampleLocationsTech.IsValid() )
    {
        m_RefineSampleLocationsTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);

        char strIntialSampleStep[8];
        sprintf_s(strIntialSampleStep, _countof(strIntialSampleStep), "%d", m_PostProcessingAttribs.m_uiInitialSampleStepInSlice);

        // Thread group size must be at least as large as initial sample step
        m_uiSampleRefinementCSThreadGroupSize = max( m_uiSampleRefinementCSMinimumThreadGroupSize, m_PostProcessingAttribs.m_uiInitialSampleStepInSlice );
        // Thread group size cannot be larger than the total number of samples in slice
        m_uiSampleRefinementCSThreadGroupSize = min( m_uiSampleRefinementCSThreadGroupSize, m_PostProcessingAttribs.m_uiMaxSamplesInSlice );
        char strThreadGroupSize[8];
        sprintf_s(strThreadGroupSize, _countof(strThreadGroupSize), "%d", m_uiSampleRefinementCSThreadGroupSize);

        D3D_SHADER_MACRO Macros[] = 
        {
            {"INITIAL_SAMPLE_STEP", strIntialSampleStep},
            {"THREAD_GROUP_SIZE"  , strThreadGroupSize },
            {NULL, NULL}
        };

        m_RefineSampleLocationsTech.CreateComputeShaderFromFile( L"RefineSampleLocations.fx", "RefineSampleLocationsCS", Macros );
    }

    //Texture2D<float2> g_tex2DCoordinates : register( t1 );
    //Texture2D<float> g_tex2DEpipolarCamSpaceZ : register( t2 );
    ID3D11ShaderResourceView *pSRVs[] = {m_ptex2DCoordianteTextureSRV, m_ptex2DEpipolarCamSpaceZSRV};
    FrameAttribs.pd3dDeviceContext->CSSetShaderResources(1, 2, pSRVs);
    FrameAttribs.pd3dDeviceContext->CSSetUnorderedAccessViews(0, 1, &m_ptex2DInterpolationSourcesUAV.p, NULL);
    // Using small group size is inefficient since a lot of SIMD lanes become idle
    m_RefineSampleLocationsTech.Apply();
    FrameAttribs.pd3dDeviceContext->Dispatch( m_PostProcessingAttribs.m_uiMaxSamplesInSlice/m_uiSampleRefinementCSThreadGroupSize,
                                              m_PostProcessingAttribs.m_uiNumEpipolarSlices,
                                              1);
    UnbindResources( FrameAttribs.pd3dDeviceContext );
}

void CLightSctrPostProcess :: MarkRayMarchingSamples(SFrameAttribs &FrameAttribs)
{
    if( !m_MarkRayMarchingSamplesInStencilTech.IsValid() )
    {
        m_MarkRayMarchingSamplesInStencilTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
        m_MarkRayMarchingSamplesInStencilTech.CreatePixelShaderFromFile( m_strEffectPath, "MarkRayMarchingSamplesInStencilPS", NULL );
        m_MarkRayMarchingSamplesInStencilTech.SetVS( m_pGenerateScreenSizeQuadVS );
        m_MarkRayMarchingSamplesInStencilTech.SetDS( m_pNoDepth_StEqual_IncrStencilDS, 1 );
        m_MarkRayMarchingSamplesInStencilTech.SetRS( m_pSolidFillNoCullRS );
        m_MarkRayMarchingSamplesInStencilTech.SetBS( m_pDefaultBS );
    }

    // Mark ray marching samples in the stencil
    // The depth stencil state is configured to pass only pixels, whose stencil value equals 1. Thus all epipolar samples with 
    // coordinates outsied the screen (generated on the previous pass) are automatically discarded. The pixel shader only
    // passes samples which are interpolated from themselves, the rest are discarded. Thus after this pass all ray
    // marching samples will be marked with 2 in stencil
    ID3D11RenderTargetView *pDummyRTV = NULL;
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, &pDummyRTV, m_ptex2DEpipolarImageDSV);
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(7, 1, &m_ptex2DInterpolationSourcesSRV.p); // Texture2D<uint2> g_tex2DInterpolationSource : register( t7 );
    RenderQuad( FrameAttribs.pd3dDeviceContext, 
                m_MarkRayMarchingSamplesInStencilTech,
                m_PostProcessingAttribs.m_uiMaxSamplesInSlice, m_PostProcessingAttribs.m_uiNumEpipolarSlices );
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
}

void CLightSctrPostProcess :: Build1DMinMaxMipMap(SFrameAttribs &FrameAttribs)
{
    {
        if( !m_RenderSliceUVDirInSMTech.IsValid() )
        {
            m_RenderSliceUVDirInSMTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
            m_RenderSliceUVDirInSMTech.CreatePixelShaderFromFile( m_strEffectPath, "RenderSliceUVDirInShadowMapTexturePS", NULL );
            m_RenderSliceUVDirInSMTech.SetVS( m_pGenerateScreenSizeQuadVS );
            m_RenderSliceUVDirInSMTech.SetDS( m_pDisableDepthTestDS );
            m_RenderSliceUVDirInSMTech.SetRS( m_pSolidFillNoCullRS );
            m_RenderSliceUVDirInSMTech.SetBS( m_pDefaultBS );
        }

        // Render [Num Slices x 1] texture containing slice direction in shadow map UV space
        FrameAttribs.pd3dDeviceContext->OMSetRenderTargets( 1, &m_ptex2DSliceUVDirInShadowMapRTV.p, NULL);
        FrameAttribs.pd3dDeviceContext->PSSetShaderResources(1, 1, &m_ptex2DCoordianteTextureSRV.p); // Texture2D<float2> g_tex2DCoordinates : register( t1 );
        RenderQuad( FrameAttribs.pd3dDeviceContext, 
                    m_RenderSliceUVDirInSMTech,
                    m_PostProcessingAttribs.m_uiNumEpipolarSlices, 1 );
        FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
    }

    {
        if( !m_InitializeMinMaxShadowMapTech.IsValid() )
        {
            m_InitializeMinMaxShadowMapTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
            m_InitializeMinMaxShadowMapTech.CreatePixelShaderFromFile( m_strEffectPath, "InitializeMinMaxShadowMapPS", NULL );
            m_InitializeMinMaxShadowMapTech.SetVS( m_pGenerateScreenSizeQuadVS );
            m_InitializeMinMaxShadowMapTech.SetDS( m_pDisableDepthTestDS );
            m_InitializeMinMaxShadowMapTech.SetRS( m_pSolidFillNoCullRS );
            m_InitializeMinMaxShadowMapTech.SetBS( m_pDefaultBS );
        }

        if( !m_ComputeMinMaxSMLevelTech.IsValid() )
        {
            m_ComputeMinMaxSMLevelTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
            m_ComputeMinMaxSMLevelTech.CreatePixelShaderFromFile( m_strEffectPath, "ComputeMinMaxShadowMapLevelPS", NULL );
            m_ComputeMinMaxSMLevelTech.SetVS( m_pGenerateScreenSizeQuadVS );
            m_ComputeMinMaxSMLevelTech.SetDS( m_pDisableDepthTestDS );
            m_ComputeMinMaxSMLevelTech.SetRS( m_pSolidFillNoCullRS );
            m_ComputeMinMaxSMLevelTech.SetBS( m_pDefaultBS );
        }
        
        // Computing min/max mip map using compute shader is much slower because a lot of threads are idle
        UINT uiXOffset = 0;
        UINT uiPrevXOffset = 0;
        UINT uiParity = 0;
        CComPtr<ID3D11Resource> presMinMaxShadowMap0, presMinMaxShadowMap1;
        m_ptex2DMinMaxShadowMapRTV[0]->GetResource(&presMinMaxShadowMap0);
        m_ptex2DMinMaxShadowMapRTV[1]->GetResource(&presMinMaxShadowMap1);
        // Note that we start rendering min/max shadow map from step == 2
        for(UINT iStep = 2; iStep <= m_PostProcessingAttribs.m_uiMaxShadowMapStep; iStep *=2, uiParity = (uiParity+1)%2 )
        {
            // Use two buffers which are in turn used as the source and destination
            FrameAttribs.pd3dDeviceContext->OMSetRenderTargets( 1, &m_ptex2DMinMaxShadowMapRTV[uiParity].p, NULL);

            if( iStep == 2 )
            {
                // At the initial pass, the shader gathers 8 depths which will be used for
                // PCF filtering at the sample location and its next neighbor along the slice 
                // and outputs min/max depths

                // Texture2D<float> g_tex2DSliceUVDirInShadowMap  : register( t2 );
                // Texture2D<float2> g_tex2DLightSpaceDepthMap    : register( t3 );
                ID3D11ShaderResourceView *pSRVs[] = {m_ptex2DSliceUVDirInShadowMapSRV, FrameAttribs.ptex2DShadowMapSRV,};
                FrameAttribs.pd3dDeviceContext->PSSetShaderResources( 2, _countof(pSRVs), pSRVs );
            }
            else
            {
                // At the subsequent passes, the shader loads two min/max values from the next finer level 
                // to compute next level of the binary tree

                // Set source and destination min/max data offsets:
                SMiscDynamicParams MiscDynamicParams;
                MiscDynamicParams.ui4SrcMinMaxLevelXOffset = uiPrevXOffset;
                MiscDynamicParams.ui4DstMinMaxLevelXOffset = uiXOffset;
                UpdateConstantBuffer(FrameAttribs.pd3dDeviceContext, m_pcbMiscParams, &MiscDynamicParams, sizeof(MiscDynamicParams));
                //cbuffer cbMiscDynamicParams : register( b4 )
                FrameAttribs.pd3dDeviceContext->PSSetConstantBuffers(4, 1, &m_pcbMiscParams.p);

                // Texture2D<float2> g_tex2DMinMaxLightSpaceDepth  : register( t4 );
                FrameAttribs.pd3dDeviceContext->PSSetShaderResources( 4, 1, &m_ptex2DMinMaxShadowMapSRV[ (uiParity+1)%2 ].p );
            }
            
            RenderQuad( FrameAttribs.pd3dDeviceContext, 
                        (iStep>2) ? m_ComputeMinMaxSMLevelTech : m_InitializeMinMaxShadowMapTech,
                        m_PostProcessingAttribs.m_uiMinMaxShadowMapResolution / iStep, m_PostProcessingAttribs.m_uiNumEpipolarSlices, 
                        uiXOffset, 0 );

            // All the data must reside in 0-th texture, so copy current level, if necessary, from 1-st texture
            if( uiParity == 1 )
            {
                D3D11_BOX SrcBox;
                SrcBox.left = uiXOffset;
                SrcBox.right = uiXOffset + m_PostProcessingAttribs.m_uiMinMaxShadowMapResolution / iStep;
                SrcBox.top = 0;
                SrcBox.bottom = m_PostProcessingAttribs.m_uiNumEpipolarSlices;
                SrcBox.front = 0;
                SrcBox.back = 1;
                FrameAttribs.pd3dDeviceContext->CopySubresourceRegion(presMinMaxShadowMap0, 0, uiXOffset, 0, 0,
                                                                        presMinMaxShadowMap1, 0, &SrcBox);
            }

            uiPrevXOffset = uiXOffset;
            uiXOffset += m_PostProcessingAttribs.m_uiMinMaxShadowMapResolution / iStep;
        }
    }
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
}

void CLightSctrPostProcess :: DoRayMarching(SFrameAttribs &FrameAttribs, UINT uiMaxStepsAlongRay)
{
    if( !m_DoRayMarchTech[0].IsValid() || !m_DoRayMarchTech[1].IsValid() )
    {
        D3D_SHADER_MACRO Macros[] = 
        {
            {"STAINED_GLASS", m_PostProcessingAttribs.m_bStainedGlass ? "1" : "0"},
            {NULL, NULL}
        };
    
        for(int iEnableMinMaxOptimzation=0; iEnableMinMaxOptimzation < 2; ++iEnableMinMaxOptimzation )
        {
            m_DoRayMarchTech[iEnableMinMaxOptimzation].SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
            m_DoRayMarchTech[iEnableMinMaxOptimzation].CreatePixelShaderFromFile( m_strEffectPath, iEnableMinMaxOptimzation ? "RayMarchMinMaxOptPS" : "RayMarchPS", Macros );
            m_DoRayMarchTech[iEnableMinMaxOptimzation].SetVS( m_pGenerateScreenSizeQuadVS );
            // Sample locations for which ray marching should be performed will be marked in stencil with 2
            m_DoRayMarchTech[iEnableMinMaxOptimzation].SetDS( m_pNoDepth_StEqual_IncrStencilDS, 2 );
            m_DoRayMarchTech[iEnableMinMaxOptimzation].SetRS( m_pSolidFillNoCullRS );
            m_DoRayMarchTech[iEnableMinMaxOptimzation].SetBS( m_pDefaultBS );
        }
    }

    //float BlackColor[] = {0,0,0,0};
    // NOTE: this is for debug purposes only:
    //FrameAttribs.pd3dDeviceContext->ClearRenderTargetView(m_ptex2DInitialScatteredLightRTV, BlackColor);
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, &m_ptex2DInitialScatteredLightRTV.p, m_ptex2DEpipolarImageDSV);

    SMiscDynamicParams MiscDynamicParams;
    MiscDynamicParams.fMaxStepsAlongRay = static_cast<float>( uiMaxStepsAlongRay );
    UpdateConstantBuffer(FrameAttribs.pd3dDeviceContext, m_pcbMiscParams, &MiscDynamicParams, sizeof(MiscDynamicParams));
    //cbuffer cbMiscDynamicParams : register( b4 )
    FrameAttribs.pd3dDeviceContext->PSSetConstantBuffers(4, 1, &m_pcbMiscParams.p);

    ID3D11ShaderResourceView *pSRVs[] = 
    {
        m_ptex2DCameraSpaceZSRV,            // Texture2D<float>  g_tex2DCamSpaceZ              : register( t0 );
        m_ptex2DCoordianteTextureSRV,       // Texture2D<float2> g_tex2DCoordinates            : register( t1 );
        m_ptex2DSliceUVDirInShadowMapSRV,   // Texture2D<float2> g_tex2DSliceUVDirInShadowMap  : register( t2 );
        FrameAttribs.ptex2DShadowMapSRV,    // Texture2D<float>  g_tex2DLightSpaceDepthMap     : register( t3 );
        m_ptex2DMinMaxShadowMapSRV[0],      // Texture2D<float2> g_tex2DMinMaxLightSpaceDepth  : register( t4 );
        FrameAttribs.ptex2DStainedGlassSRV  // Texture2D<float4> g_tex2DStainedGlassColorDepth : register( t5 );
    };
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, _countof(pSRVs), pSRVs);
    
    // Depth stencil view now contains 2 for these pixels, for which ray marchings is to be performed
    // Depth stencil state is configured to pass only these pixels and discard the rest
    RenderQuad( FrameAttribs.pd3dDeviceContext,
                m_DoRayMarchTech[ m_PostProcessingAttribs.m_bMinMaxShadowMapOptimization ? 1 : 0 ],
                m_PostProcessingAttribs.m_uiMaxSamplesInSlice, m_PostProcessingAttribs.m_uiNumEpipolarSlices );
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
}

void CLightSctrPostProcess :: InterpolateInsctrIrradiance(SFrameAttribs &FrameAttribs)
{
    if( !m_InterpolateIrradianceTech.IsValid() )
    {
        D3D_SHADER_MACRO Macros[] = 
        {
            {"REFINE_INSCTR_INTEGRAL", m_PostProcessingAttribs.m_bRefineInsctrIntegral ? "1" : "0"},
            {"STAINED_GLASS", m_PostProcessingAttribs.m_bStainedGlass ? "1" : "0"},
            {NULL, NULL}
        };
                
        m_InterpolateIrradianceTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
        m_InterpolateIrradianceTech.CreatePixelShaderFromFile( m_strEffectPath, "InterpolateIrradiancePS", Macros );
        m_InterpolateIrradianceTech.SetVS( m_pGenerateScreenSizeQuadVS );
        m_InterpolateIrradianceTech.SetDS( m_pDisableDepthTestDS );
        m_InterpolateIrradianceTech.SetRS( m_pSolidFillNoCullRS );
        m_InterpolateIrradianceTech.SetBS( m_pDefaultBS );
    }

    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, &m_ptex2DScatteredLightRTV.p, m_ptex2DEpipolarImageDSV);
    
    ID3D11ShaderResourceView *pSRVs[] = 
    {
        m_ptex2DEpipolarSampleWorldPosSRV,  // Texture2D<float4> g_tex2DEpipolarSampleWorldPos : register( t0 );
        m_ptex2DCoordianteTextureSRV,       // Texture2D<float2> g_tex2DCoordinates            : register( t1 );
        m_ptex2DEpipolarCamSpaceZSRV,       // Texture2D<float2> g_tex2DEpipolarCamSpaceZ      : register( t2 );
        FrameAttribs.ptex2DShadowMapSRV,    // Texture2D<float>  g_tex2DLightSpaceDepthMap     : register( t3 );
        NULL,                               // Unused
        FrameAttribs.ptex2DStainedGlassSRV, // Texture2D<float4> g_tex2DStainedGlassColorDepth : register( t5 );
        m_ptex2DInitialScatteredLightSRV,   // Texture2D<uint2>  g_tex2DInitialInsctrIrradiance: register( t6 );
        m_ptex2DInterpolationSourcesSRV     // Texture2D<float3> g_tex2DInterpolationSource    : register( t7 );
    };
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, _countof(pSRVs), pSRVs);
    RenderQuad( FrameAttribs.pd3dDeviceContext,
                m_InterpolateIrradianceTech,
                m_PostProcessingAttribs.m_uiMaxSamplesInSlice, m_PostProcessingAttribs.m_uiNumEpipolarSlices );
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(0, NULL, NULL);
}

void CLightSctrPostProcess :: UnwarpEpipolarScattering(SFrameAttribs &FrameAttribs)
{
    if( !m_UnwarpEpipolarSctrImgTech[0].IsValid() || !m_UnwarpEpipolarSctrImgTech[1].IsValid() )
    {
        D3D_SHADER_MACRO Macros[] =
        {
            {"OPTIMIZE_SAMPLE_LOCATIONS", m_PostProcessingAttribs.m_bOptimizeSampleLocations ? "1" : "0"},
            {NULL, NULL}
        };
        for(int iApplyBackground = 0; iApplyBackground < 2; iApplyBackground++)
        {
            m_UnwarpEpipolarSctrImgTech[iApplyBackground].SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
            m_UnwarpEpipolarSctrImgTech[iApplyBackground].CreatePixelShaderFromFile( m_strEffectPath, iApplyBackground ? "ApplyInscatteredRadiancePS" : "UnwarpEpipolarInsctrImagePS", Macros );
            m_UnwarpEpipolarSctrImgTech[iApplyBackground].SetVS( m_pGenerateScreenSizeQuadVS );
            m_UnwarpEpipolarSctrImgTech[iApplyBackground].SetDS( m_pDisableDepthTestIncrStencilDS );
            m_UnwarpEpipolarSctrImgTech[iApplyBackground].SetRS( m_pSolidFillNoCullRS );
            m_UnwarpEpipolarSctrImgTech[iApplyBackground].SetBS( m_pDefaultBS );
        }
    }

    ID3D11ShaderResourceView *pSRVs[] = 
    {
        m_ptex2DCameraSpaceZSRV,                // Texture2D<float>  g_tex2DCamSpaceZ              : register( t0 );
        FrameAttribs.ptex2DSrcColorBufferSRV,   // Texture2D<float4> g_tex2DColorBuffer            : register( t1 );
        m_ptex2DEpipolarCamSpaceZSRV,           // Texture2D<float>  g_tex2DEpipolarCamSpaceZ      : register( t2 );
        m_ptex2DScatteredLightSRV,              // Texture2D<float3> g_tex2DScatteredColor         : register( t3 );
    };
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, _countof(pSRVs), pSRVs);

    // If downscale factor is 1, then we will unwarp inscattering image and apply it to attenuated backgorund
    // If downscale factor is > 1, then we will unwarp inscattering image only, then upscale it and only after that apply to attenuated backgorund
    // Depth stenci state is configured to always increase stencil values. Thus these samples that are sucessully interpolated
    // will have 1 in stencil. The remaining will be discarded and as a result keep 0 value. These samples will be lately correct
    // through additional ray marching pass
    RenderQuad( FrameAttribs.pd3dDeviceContext,
                m_UnwarpEpipolarSctrImgTech[ (m_PostProcessingAttribs.m_fDownscaleFactor == 1.f) ? 1 : 0 ] );
}

void CLightSctrPostProcess :: FixInscatteringAtDepthBreaks(SFrameAttribs &FrameAttribs, bool bAttenuateBackground, UINT uiMaxStepsAlongRay)
{
    if( !m_FixInsctrAtDepthBreaksTech[0].IsValid() || !m_FixInsctrAtDepthBreaksTech[1].IsValid() )
    {
        D3D_SHADER_MACRO Macros[] = 
        {
            {"STAINED_GLASS", m_PostProcessingAttribs.m_bStainedGlass ? "1" : "0"},
            {NULL, NULL}
        };
        for(int iApplyBackground = 0; iApplyBackground < 2; iApplyBackground++)
        {
            m_FixInsctrAtDepthBreaksTech[iApplyBackground].SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
            m_FixInsctrAtDepthBreaksTech[iApplyBackground].CreatePixelShaderFromFile( m_strEffectPath, iApplyBackground ? "FixAndApplyInscatteredRadiancePS" : "FixInscatteredRadiancePS", Macros );
            m_FixInsctrAtDepthBreaksTech[iApplyBackground].SetVS( m_pGenerateScreenSizeQuadVS );
            m_FixInsctrAtDepthBreaksTech[iApplyBackground].SetDS( m_pNoDepth_StEqual_IncrStencilDS, 0 );
            m_FixInsctrAtDepthBreaksTech[iApplyBackground].SetRS( m_pSolidFillNoCullRS );
            m_FixInsctrAtDepthBreaksTech[iApplyBackground].SetBS( m_pDefaultBS );
        }
    }

    ID3D11ShaderResourceView *pSRVs[] = 
    {
        m_ptex2DCameraSpaceZSRV,                // Texture2D<float>  g_tex2DCamSpaceZ              : register( t0 );
        FrameAttribs.ptex2DSrcColorBufferSRV,   // Texture2D<float4> g_tex2DColorBuffer            : register( t1 );
        m_ptex2DSliceUVDirInShadowMapSRV,       // Texture2D<float2> g_tex2DSliceUVDirInShadowMap  : register( t2 );
        FrameAttribs.ptex2DShadowMapSRV,        // Texture2D<float>  g_tex2DLightSpaceDepthMap     : register( t3 );
        m_ptex2DMinMaxShadowMapSRV[0],          // Texture2D<float2> g_tex2DMinMaxLightSpaceDepth  : register( t4 );
        FrameAttribs.ptex2DStainedGlassSRV      // Texture2D<float4> g_tex2DStainedGlassColorDepth : register( t5 );
    };
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, _countof(pSRVs), pSRVs);
    
    SMiscDynamicParams MiscDynamicParams;
    MiscDynamicParams.fMaxStepsAlongRay = static_cast<float>( uiMaxStepsAlongRay );
    UpdateConstantBuffer(FrameAttribs.pd3dDeviceContext, m_pcbMiscParams, &MiscDynamicParams, sizeof(MiscDynamicParams));
    //cbuffer cbMiscDynamicParams : register( b4 )
    FrameAttribs.pd3dDeviceContext->PSSetConstantBuffers(4, 1, &m_pcbMiscParams.p);

    RenderQuad( FrameAttribs.pd3dDeviceContext,
                m_FixInsctrAtDepthBreaksTech[bAttenuateBackground ? 1 : 0 ] );
}

void CLightSctrPostProcess :: UpscaleInscatteringRadiance(SFrameAttribs &FrameAttribs)
{
    if( !m_UpscaleInsctrdRadianceTech.IsValid() )
    {
        m_UpscaleInsctrdRadianceTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
        m_UpscaleInsctrdRadianceTech.CreatePixelShaderFromFile( m_strEffectPath, "UpscaleInscatteredRadiancePS", NULL );
        m_UpscaleInsctrdRadianceTech.SetVS( m_pGenerateScreenSizeQuadVS );
        m_UpscaleInsctrdRadianceTech.SetDS( m_pDisableDepthTestIncrStencilDS );
        m_UpscaleInsctrdRadianceTech.SetRS( m_pSolidFillNoCullRS );
        m_UpscaleInsctrdRadianceTech.SetBS( m_pDefaultBS );

    }

    ID3D11ShaderResourceView *pSRVs[] = 
    {
        m_ptex2DCameraSpaceZSRV,                // Texture2D<float>  g_tex2DCamSpaceZ              : register( t0 );
        FrameAttribs.ptex2DSrcColorBufferSRV,   // Texture2D<float4> g_tex2DColorBuffer            : register( t1 );
        m_ptex2DDownscaledScatteredLightSRV     // Texture2D<float3> g_tex2DDownscaledInsctrRadiance: register( t2 );
    };
    FrameAttribs.pd3dDeviceContext->PSSetShaderResources(0, _countof(pSRVs), pSRVs);
    
    RenderQuad( FrameAttribs.pd3dDeviceContext, m_UpscaleInsctrdRadianceTech );
}

void CLightSctrPostProcess :: RenderSampleLocations(SFrameAttribs &FrameAttribs)
{
    if( !m_RenderSampleLocationsTech.IsValid() )
    {
        m_RenderSampleLocationsTech.SetDeviceAndContext(FrameAttribs.pd3dDevice, FrameAttribs.pd3dDeviceContext);
        m_RenderSampleLocationsTech.CreateVGPShadersFromFile( m_strEffectPath, "PassThroughVS", "RenderSamplePositionsGS", "RenderSampleLocationsPS", NULL );
        m_RenderSampleLocationsTech.SetDS( m_pDisableDepthTestDS );
        m_RenderSampleLocationsTech.SetRS( m_pSolidFillNoCullRS );
        D3D11_BLEND_DESC OverBlendStateDesc;
        ZeroMemory(&OverBlendStateDesc, sizeof(OverBlendStateDesc));
        OverBlendStateDesc.IndependentBlendEnable = FALSE;
        OverBlendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        OverBlendStateDesc.RenderTarget[0].BlendEnable    = TRUE;
        OverBlendStateDesc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
        OverBlendStateDesc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        OverBlendStateDesc.RenderTarget[0].DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
        OverBlendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        OverBlendStateDesc.RenderTarget[0].SrcBlend       = D3D11_BLEND_SRC_ALPHA;
        OverBlendStateDesc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        CComPtr<ID3D11BlendState> pOverBS;
        HRESULT hr;
        V( FrameAttribs.pd3dDevice->CreateBlendState( &OverBlendStateDesc, &pOverBS) );
        m_RenderSampleLocationsTech.SetBS( pOverBS );
    }

    ID3D11ShaderResourceView *pSRVs[] = 
    {
        NULL,
        m_ptex2DCoordianteTextureSRV,               // Texture2D<float2> g_tex2DCoordinates            : register( t1 );
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        m_ptex2DInterpolationSourcesSRV,            // Texture2D<uint2>  g_tex2DInterpolationSource    : register( t7 );
    };
    FrameAttribs.pd3dDeviceContext->GSSetShaderResources(0, _countof(pSRVs), pSRVs);

    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, &FrameAttribs.pDstRTV, NULL);
    FrameAttribs.pd3dDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
    UINT offset[1] = {0};
    UINT stride[1] = {0};
    ID3D11Buffer *ppBuffers[1] = {0};
    // Set the device's first and only vertex buffer with zero stride and offset
    FrameAttribs.pd3dDeviceContext->IASetVertexBuffers(0,1,ppBuffers,stride,offset);
    // There is no input-layout object and the primitive topology is triangle strip
    FrameAttribs.pd3dDeviceContext->IASetInputLayout(NULL);
    m_RenderSampleLocationsTech.Apply();
    FrameAttribs.pd3dDeviceContext->Draw(m_PostProcessingAttribs.m_uiMaxSamplesInSlice * m_PostProcessingAttribs.m_uiNumEpipolarSlices,0);
    UnbindResources( FrameAttribs.pd3dDeviceContext );
}

void CLightSctrPostProcess :: PerformPostProcessing(SFrameAttribs &FrameAttribs,
                                                    SPostProcessingAttribs &PPAttribs)
{
    HRESULT hr;
    if( PPAttribs.m_uiMaxSamplesInSlice != m_PostProcessingAttribs.m_uiMaxSamplesInSlice || 
        PPAttribs.m_uiNumEpipolarSlices != m_PostProcessingAttribs.m_uiNumEpipolarSlices )
    {
        m_ptex2DCoordianteTextureRTV.Release();
        m_ptex2DCoordianteTextureSRV.Release();
        m_RefineSampleLocationsTech.Release();
    }
    
    if( PPAttribs.m_uiMinMaxShadowMapResolution != m_PostProcessingAttribs.m_uiMinMaxShadowMapResolution || 
        PPAttribs.m_uiNumEpipolarSlices != m_PostProcessingAttribs.m_uiNumEpipolarSlices )
    {
        for(int i=0; i < _countof(m_ptex2DMinMaxShadowMapSRV); ++i)
            m_ptex2DMinMaxShadowMapSRV[i].Release();
        for(int i=0; i < _countof(m_ptex2DMinMaxShadowMapRTV); ++i)
            m_ptex2DMinMaxShadowMapRTV[i].Release();
    }
    
    if( PPAttribs.m_uiInitialSampleStepInSlice != m_PostProcessingAttribs.m_uiInitialSampleStepInSlice )
    {
        // Release sample locations refining compute shader so that it will be re-compiled
        m_RefineSampleLocationsTech.Release();
    }

    if( PPAttribs.m_bOptimizeSampleLocations != m_PostProcessingAttribs.m_bOptimizeSampleLocations )
    {
        m_RendedCoordTexTech.Release();
        for(int i=0; i < _countof(m_UnwarpEpipolarSctrImgTech); ++i)
            m_UnwarpEpipolarSctrImgTech[i].Release();
    }
    
    if( PPAttribs.m_bStainedGlass != m_PostProcessingAttribs.m_bStainedGlass )
    {
        for(int i=0; i < _countof(m_DoRayMarchTech); ++i)
            m_DoRayMarchTech[i].Release();
        for(int i=0; i < _countof(m_FixInsctrAtDepthBreaksTech); ++i)
            m_FixInsctrAtDepthBreaksTech[i].Release();
        m_InterpolateIrradianceTech.Release();
    }
    
    if( PPAttribs.m_bRefineInsctrIntegral != m_PostProcessingAttribs.m_bRefineInsctrIntegral )
    {
        m_InterpolateIrradianceTech.Release();
        m_RendedCoordTexTech.Release();
    }

    if( m_PostProcessingAttribs.m_fDownscaleFactor != PPAttribs.m_fDownscaleFactor )
    {
        m_ptex2DDownscaledScatteredLightSRV.Release();
        m_ptex2DDownscaledScatteredLightRTV.Release();
        m_ptex2DDownscaledDSV.Release();
    }

    bool bRecomputeSctrCoeffs = m_PostProcessingAttribs.m_fDistanceScaler != PPAttribs.m_fDistanceScaler || 
                                m_PostProcessingAttribs.m_f4RayleighBeta != PPAttribs.m_f4RayleighBeta ||
                                m_PostProcessingAttribs.m_f4MieBeta != PPAttribs.m_f4MieBeta;

    m_PostProcessingAttribs = PPAttribs;
    
    if( bRecomputeSctrCoeffs )
        ComputeScatteringCoefficients(FrameAttribs.pd3dDeviceContext);

    if( !m_ptex2DCoordianteTextureRTV || !m_ptex2DCoordianteTextureSRV )
    {
        V( CreateTextures(FrameAttribs.pd3dDevice) );
    }

    if( m_PostProcessingAttribs.m_fDownscaleFactor > 1 && !m_ptex2DDownscaledScatteredLightSRV )
    {
        V( CreateDownscaledInscatteringTextures(FrameAttribs.pd3dDevice, 
            static_cast<UINT>( m_uiBackBufferWidth / m_PostProcessingAttribs.m_fDownscaleFactor ),
            static_cast<UINT>( m_uiBackBufferHeight / m_PostProcessingAttribs.m_fDownscaleFactor ) ) );
    }

    if( !m_ptex2DMinMaxShadowMapSRV[0] )
    {
        V( CreateMinMaxShadowMap(FrameAttribs.pd3dDevice) );
    }

    const SLightAttribs &LightAttribs = FrameAttribs.LightAttribs;
    const SCameraAttribs &CamAttribs = FrameAttribs.CameraAttribs;

    m_PostProcessingAttribs.m_f2CoordinateTexDim.x = static_cast<float>( m_PostProcessingAttribs.m_uiMaxSamplesInSlice );
    m_PostProcessingAttribs.m_f2CoordinateTexDim.y = static_cast<float>( m_PostProcessingAttribs.m_uiNumEpipolarSlices );

    m_PostProcessingAttribs.m_f2ScreenResolution.x = static_cast<float>( m_uiBackBufferWidth );
    m_PostProcessingAttribs.m_f2ScreenResolution.y = static_cast<float>( m_uiBackBufferHeight );

    UpdateConstantBuffer(FrameAttribs.pd3dDeviceContext, m_pcbCameraAttribs, &CamAttribs, sizeof(CamAttribs));
    UpdateConstantBuffer(FrameAttribs.pd3dDeviceContext, m_pcbLightAttribs, &LightAttribs, sizeof(LightAttribs));
    UpdateConstantBuffer(FrameAttribs.pd3dDeviceContext, m_pcbPostProcessingAttribs, &m_PostProcessingAttribs, sizeof(m_PostProcessingAttribs));
    
    // Set constant buffers that will be used by all pixel shaders and compute shader
    ID3D11Buffer *pCBs[] = {m_pcbPostProcessingAttribs, m_pcbMediaAttribs, m_pcbCameraAttribs, m_pcbLightAttribs};
    FrameAttribs.pd3dDeviceContext->GSSetConstantBuffers(0, _countof(pCBs), pCBs);
    FrameAttribs.pd3dDeviceContext->PSSetConstantBuffers(0, _countof(pCBs), pCBs);
    FrameAttribs.pd3dDeviceContext->CSSetConstantBuffers(0, _countof(pCBs), pCBs);
    

    ID3D11SamplerState *pSamplers[] = { m_psamLinearClamp, m_psamLinearBorder0, m_psamLinearUClampVWrap, m_psamComparison };
    FrameAttribs.pd3dDeviceContext->PSSetSamplers(0, _countof(pSamplers), pSamplers);

    D3D11_VIEWPORT OrigViewPort;
    UINT iNumOldViewports = 1;
    FrameAttribs.pd3dDeviceContext->RSGetViewports(&iNumOldViewports, &OrigViewPort);

    ReconstructCameraSpaceZ(FrameAttribs);
    
    // Render coordinate texture and camera space z for epipolar location
    RenderCoordinateTexture(FrameAttribs);

    // Refine initial ray marching samples
    RefineSampleLocations(FrameAttribs);

    // Mark all ray marching samples in stencil
    MarkRayMarchingSamples( FrameAttribs );

    // Build min/max mip map
    if( m_PostProcessingAttribs.m_bMinMaxShadowMapOptimization )
    {
        Build1DMinMaxMipMap(FrameAttribs);
    }

    // Perform ray marching for selected samples
    DoRayMarching(FrameAttribs, m_PostProcessingAttribs.m_uiShadowMapResolution);
    
    // Interpolate ray marching samples onto the rest of samples
    InterpolateInsctrIrradiance(FrameAttribs);

    const UINT uiMaxStepsAlongRayAtDepthBreak0 = min(m_PostProcessingAttribs.m_uiShadowMapResolution/4, 256);
    const UINT uiMaxStepsAlongRayAtDepthBreak1 = min(m_PostProcessingAttribs.m_uiShadowMapResolution/8, 128);
        
    ID3D11DepthStencilView *pDSV = (m_PostProcessingAttribs.m_fDownscaleFactor == 1.f) ? m_ptex2DScreenSizeDSV : m_ptex2DDownscaledDSV;
    ID3D11RenderTargetView *pRTV[] = { (m_PostProcessingAttribs.m_fDownscaleFactor == 1.f) ? FrameAttribs.pDstRTV : m_ptex2DDownscaledScatteredLightRTV };
    FrameAttribs.pd3dDeviceContext->ClearDepthStencilView(pDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0, 0);

    // Transform inscattering irradiance from epipolar coordinates back to rectangular
    FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, pRTV, pDSV);
    UnwarpEpipolarScattering(FrameAttribs);
    
    // Correct inscattering for pixels, for which no suitable interpolation sources were found
    if( m_PostProcessingAttribs.m_bCorrectScatteringAtDepthBreaks )
    {
        FixInscatteringAtDepthBreaks(FrameAttribs, m_PostProcessingAttribs.m_fDownscaleFactor == 1.f, uiMaxStepsAlongRayAtDepthBreak0);
    }

    if( m_PostProcessingAttribs.m_fDownscaleFactor > 1.f )
    {
        FrameAttribs.pd3dDeviceContext->OMSetRenderTargets(1, &FrameAttribs.pDstRTV, m_ptex2DScreenSizeDSV);
        FrameAttribs.pd3dDeviceContext->ClearDepthStencilView(m_ptex2DScreenSizeDSV, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0, 0);
        UpscaleInscatteringRadiance( FrameAttribs );

        if( m_PostProcessingAttribs.m_bCorrectScatteringAtDepthBreaks )
        {
            FixInscatteringAtDepthBreaks(FrameAttribs, true, uiMaxStepsAlongRayAtDepthBreak1);
        }
    }

    if( m_PostProcessingAttribs.m_bShowSampling )
    {
        RenderSampleLocations(FrameAttribs);
    }
}

HRESULT CLightSctrPostProcess :: CreateDownscaledInscatteringTextures(ID3D11Device* pd3dDevice, UINT Width, UINT Height)
{
    HRESULT hr;

    D3D11_TEXTURE2D_DESC DownscaledInscatteringTexDesc = 
    {
        Width,          //UINT Width;
        Height,          //UINT Height;
        1,                                                     //UINT MipLevels;
        1,                                                     //UINT ArraySize;
        // R8G8B8A8_UNORM texture does not provide sufficient precision which causes 
        // interpolation artifacts especially noticeable in low intensity regions
        DXGI_FORMAT_R16G16B16A16_FLOAT,                            //DXGI_FORMAT Format;
        {1,0},                                                 //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                                   //D3D11_USAGE Usage;
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, //UINT BindFlags;
        0,                                                     //UINT CPUAccessFlags;
        0,                                                     //UINT MiscFlags;
    };

    m_ptex2DDownscaledScatteredLightSRV.Release();
    m_ptex2DDownscaledScatteredLightRTV.Release();

    CComPtr<ID3D11Texture2D> ptex2DDownscaledInscatteringTex;
    // Create 2-D texture, shader resource and target view buffers on the device
    V_RETURN( pd3dDevice->CreateTexture2D( &DownscaledInscatteringTexDesc, NULL, &ptex2DDownscaledInscatteringTex) );
    V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DDownscaledInscatteringTex, NULL, &m_ptex2DDownscaledScatteredLightSRV)  );
    V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DDownscaledInscatteringTex, NULL, &m_ptex2DDownscaledScatteredLightRTV)  );
    
    DownscaledInscatteringTexDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    DownscaledInscatteringTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    m_ptex2DDownscaledDSV.Release();
    CComPtr<ID3D11Texture2D> ptex2DDownscaledDepthStencil;
    V_RETURN( pd3dDevice->CreateTexture2D( &DownscaledInscatteringTexDesc, NULL, &ptex2DDownscaledDepthStencil) );
    V_RETURN( pd3dDevice->CreateDepthStencilView( ptex2DDownscaledDepthStencil, NULL, &m_ptex2DDownscaledDSV)  );

    return S_OK;
}

HRESULT CLightSctrPostProcess :: CreateTextures(ID3D11Device* pd3dDevice)
{
    HRESULT hr;

    D3D11_TEXTURE2D_DESC CoordinateTexDesc = 
    {
        m_PostProcessingAttribs.m_uiMaxSamplesInSlice,          //UINT Width;
        m_PostProcessingAttribs.m_uiNumEpipolarSlices,          //UINT Height;
        1,                                                     //UINT MipLevels;
        1,                                                     //UINT ArraySize;
        DXGI_FORMAT_R32G32_FLOAT,                              //DXGI_FORMAT Format;
        {1,0},                                                 //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                                   //D3D11_USAGE Usage;
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, //UINT BindFlags;
        0,                                                     //UINT CPUAccessFlags;
        0,                                                     //UINT MiscFlags;
    };

    {
        CComPtr<ID3D11Texture2D> ptex2DCoordianteTexture;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &CoordinateTexDesc, NULL, &ptex2DCoordianteTexture) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DCoordianteTexture, NULL, &m_ptex2DCoordianteTextureSRV)  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DCoordianteTexture, NULL, &m_ptex2DCoordianteTextureRTV)  );
    }

    {
        m_ptex2DInterpolationSourcesSRV.Release();
        m_ptex2DInterpolationSourcesUAV.Release();
        D3D11_TEXTURE2D_DESC InterpolationSourceTexDesc = CoordinateTexDesc;
        InterpolationSourceTexDesc.Format = DXGI_FORMAT_R16G16_UINT;
        InterpolationSourceTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        CComPtr<ID3D11Texture2D> ptex2DInterpolationSource;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &InterpolationSourceTexDesc, NULL, &ptex2DInterpolationSource) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DInterpolationSource, NULL, &m_ptex2DInterpolationSourcesSRV)  );
        V_RETURN( pd3dDevice->CreateUnorderedAccessView( ptex2DInterpolationSource, NULL, &m_ptex2DInterpolationSourcesUAV)  );
    }

    {
        m_ptex2DEpipolarCamSpaceZSRV.Release();
        m_ptex2DEpipolarCamSpaceZRTV.Release();
        D3D11_TEXTURE2D_DESC EpipolarCamSpaceZTexDesc = CoordinateTexDesc;
        EpipolarCamSpaceZTexDesc.Format = DXGI_FORMAT_R32_FLOAT;
        EpipolarCamSpaceZTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        CComPtr<ID3D11Texture2D> ptex2DEpipolarCamSpace;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &EpipolarCamSpaceZTexDesc, NULL, &ptex2DEpipolarCamSpace) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DEpipolarCamSpace, NULL, &m_ptex2DEpipolarCamSpaceZSRV)  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DEpipolarCamSpace, NULL, &m_ptex2DEpipolarCamSpaceZRTV)  );
    }

    {
        m_ptex2DEpipolarSampleWorldPosSRV.Release();
        m_ptex2DEpipolarSampleWorldPosRTV.Release();
        D3D11_TEXTURE2D_DESC EpipolarSampleWorldPosTexDesc = CoordinateTexDesc;
        EpipolarSampleWorldPosTexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        EpipolarSampleWorldPosTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        CComPtr<ID3D11Texture2D> ptex2DEpipolarSampleWorldPos;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &EpipolarSampleWorldPosTexDesc, NULL, &ptex2DEpipolarSampleWorldPos) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DEpipolarSampleWorldPos, NULL, &m_ptex2DEpipolarSampleWorldPosSRV)  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DEpipolarSampleWorldPos, NULL, &m_ptex2DEpipolarSampleWorldPosRTV)  );
    }

    {
        m_ptex2DScatteredLightSRV.Release();
        m_ptex2DScatteredLightRTV.Release();
        D3D11_TEXTURE2D_DESC ScatteredLightTexDesc = CoordinateTexDesc;
        // R8G8B8A8_UNORM texture does not provide sufficient precision which causes 
        // interpolation artifacts especially noticeable in low intensity regions
        ScatteredLightTexDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        CComPtr<ID3D11Texture2D> ptex2DScatteredLight;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &ScatteredLightTexDesc, NULL, &ptex2DScatteredLight) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DScatteredLight, NULL, &m_ptex2DScatteredLightSRV)  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DScatteredLight, NULL, &m_ptex2DScatteredLightRTV)  );

        m_ptex2DInitialScatteredLightSRV.Release();
        m_ptex2DInitialScatteredLightRTV.Release();
        CComPtr<ID3D11Texture2D> ptex2DInitialScatteredLight;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &ScatteredLightTexDesc, NULL, &ptex2DInitialScatteredLight) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DInitialScatteredLight, NULL, &m_ptex2DInitialScatteredLightSRV)  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DInitialScatteredLight, NULL, &m_ptex2DInitialScatteredLightRTV)  );
    }

    {
        m_ptex2DEpipolarImageDSV.Release();
        D3D11_TEXTURE2D_DESC EpipolarDeptTexDesc = CoordinateTexDesc;
        EpipolarDeptTexDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        EpipolarDeptTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        CComPtr<ID3D11Texture2D> ptex2DEpipolarImage;
        V_RETURN( pd3dDevice->CreateTexture2D( &EpipolarDeptTexDesc, NULL, &ptex2DEpipolarImage) );
        V_RETURN( pd3dDevice->CreateDepthStencilView( ptex2DEpipolarImage, NULL, &m_ptex2DEpipolarImageDSV) );
    }

    {
        m_ptex2DSliceUVDirInShadowMapSRV.Release();
        m_ptex2DSliceUVDirInShadowMapRTV.Release();
        D3D11_TEXTURE2D_DESC SliceUVDirInShadowMapTexDesc = CoordinateTexDesc;
        SliceUVDirInShadowMapTexDesc.Width = m_PostProcessingAttribs.m_uiNumEpipolarSlices;
        SliceUVDirInShadowMapTexDesc.Height = 1;
        SliceUVDirInShadowMapTexDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
        CComPtr<ID3D11Texture2D> ptex2DSliceUVDirInShadowMap;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &SliceUVDirInShadowMapTexDesc, NULL, &ptex2DSliceUVDirInShadowMap) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DSliceUVDirInShadowMap, NULL, &m_ptex2DSliceUVDirInShadowMapSRV)  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DSliceUVDirInShadowMap, NULL, &m_ptex2DSliceUVDirInShadowMapRTV)  );
    }
        
    return S_OK;
}

HRESULT CLightSctrPostProcess :: CreateMinMaxShadowMap(ID3D11Device* pd3dDevice)
{
    D3D11_TEXTURE2D_DESC MinMaxShadowMapTexDesc = 
    {
        // Min/max shadow map does not contain finest resolution level of the shadow map
        m_PostProcessingAttribs.m_uiMinMaxShadowMapResolution, //UINT Width;
        m_PostProcessingAttribs.m_uiNumEpipolarSlices,         //UINT Height;
        1,                                                     //UINT MipLevels;
        1,                                                     //UINT ArraySize;
        DXGI_FORMAT_R16G16_FLOAT,                              //DXGI_FORMAT Format;
        {1,0},                                                 //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                                   //D3D11_USAGE Usage;
        D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, //UINT BindFlags;
        0,                                                     //UINT CPUAccessFlags;
        0,                                                     //UINT MiscFlags;
    };
    
    HRESULT hr;
    for(int i=0; i < 2; ++i)
    {
        m_ptex2DMinMaxShadowMapSRV[i].Release();
        m_ptex2DMinMaxShadowMapRTV[i].Release();

        CComPtr<ID3D11Texture2D> ptex2DMinMaxShadowMap;
        // Create 2-D texture, shader resource and target view buffers on the device
        V_RETURN( pd3dDevice->CreateTexture2D( &MinMaxShadowMapTexDesc, NULL, &ptex2DMinMaxShadowMap) );
        V_RETURN( pd3dDevice->CreateShaderResourceView( ptex2DMinMaxShadowMap, NULL, &m_ptex2DMinMaxShadowMapSRV[i])  );
        V_RETURN( pd3dDevice->CreateRenderTargetView( ptex2DMinMaxShadowMap, NULL, &m_ptex2DMinMaxShadowMapRTV[i])  );
    }

    return S_OK;
}

void CLightSctrPostProcess :: ComputeSunColor( const D3DXVECTOR3 &vDirectionOnSun,
                                         D3DXVECTOR4 &f4SunColorAtGround,
                                         D3DXVECTOR4 &f4AmbientLight)
{
    // For details, see "A practical Analytic Model for Daylight" by Preetham & Hoffman, p.21

    // First, set the direction on sun parameters
        
    // Compute the ambient light values
    float zenithFactor = min( max(vDirectionOnSun.y, 0.0f), 1.0f);
    f4AmbientLight.x = zenithFactor*0.3f;
    f4AmbientLight.y = zenithFactor*0.2f;
    f4AmbientLight.z = 0.25f;
    f4AmbientLight.w = 0.0f;
    
    // Compute theta as the angle from the noon (zenith) position in radians
    float theta = acos( vDirectionOnSun.y );

    // theta = (m_PPParams.m_fTimeOfDay < D3DX_PI) ? m_PPParams.m_fTimeOfDay : ((float)D3DX_PI*2 - m_PPParams.m_fTimeOfDay);

    theta = (theta < D3DX_PI) ? theta : ((float)D3DX_PI*2 - theta);

    // Angles greater than the horizon are clamped
    theta = min( max(theta, 0.0f), (float)D3DX_PI/2);

    // Beta is an Angstrom's turbidity coefficient and is approximated by:
    float beta = 0.04608365822050f * m_fTurbidity - 0.04586025928522f;
    float RelativeOpticalMass = 1.0f / ( cosf(theta) + 0.15f * powf( (float) (93.885f - theta/D3DX_PI*180.0f), -1.253f ) );

    // Constants for lambda
    float lambda[] = 
    {
        0.65f, // red
        0.57f, // green
        0.475f //blue
    };

    // Compute the transmittance for each wave length
    for(int i=0; i<3; ++i)
    {
        // Transmittance due to Rayleigh Scattering
        float tauR = expf( -RelativeOpticalMass * 0.008735f * powf(lambda[i], -4.08f));

        // Aerosal (water + dust) attenuation
        // Paticle size ratio set at (alpha = 1.3) 
        // Alpha - ratio of small to large particle sizes. (0:4,usually 1.3)
        const float alpha = 1.3f;
        float tauA = expf( -RelativeOpticalMass * beta * powf(lambda[i], -alpha)); 

        ((float*)&f4SunColorAtGround)[i] = tauR * tauA; 
    }

    f4SunColorAtGround.w = 2;
}

void CLightSctrPostProcess :: ComputeScatteringCoefficients(ID3D11DeviceContext *pDeviceCtx)
{
    m_MediaParams.f4TotalRayleighBeta = m_PostProcessingAttribs.m_f4RayleighBeta;
    // Scale scattering coefficients to match the scene scale
    const float fRayleighBetaMultiplier = m_PostProcessingAttribs.m_fDistanceScaler;
    m_MediaParams.f4TotalRayleighBeta *= fRayleighBetaMultiplier;
    
    m_MediaParams.f4AngularRayleighBeta = m_MediaParams.f4TotalRayleighBeta * static_cast<float>(3.0/(16.0*M_PI));

    m_MediaParams.f4TotalMieBeta = m_PostProcessingAttribs.m_f4MieBeta;
    const float fBetaMieMultiplier = 0.0005f * m_PostProcessingAttribs.m_fDistanceScaler;
    m_MediaParams.f4TotalMieBeta *= fBetaMieMultiplier;
    m_MediaParams.f4AngularMieBeta = m_MediaParams.f4TotalMieBeta / static_cast<float>(D3DX_PI*4);


    m_MediaParams.f4SummTotalBeta = m_MediaParams.f4TotalRayleighBeta + m_MediaParams.f4TotalMieBeta;

    const float fGH_g = 0.98f;
    m_MediaParams.f4HG_g.x = 1 - fGH_g*fGH_g;
    m_MediaParams.f4HG_g.y = 1 + fGH_g*fGH_g;
    m_MediaParams.f4HG_g.z = -2*fGH_g;
    m_MediaParams.f4HG_g.w = 1.0;

    if( pDeviceCtx && m_pcbMediaAttribs )
    {
        pDeviceCtx->UpdateSubresource(m_pcbMediaAttribs, 0, NULL, &m_MediaParams, 0, 0);
    }
}
