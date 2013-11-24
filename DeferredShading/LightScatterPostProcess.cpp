#include "LightScatterPostProcess.h"
#include <sstream>
#include <vector>

LightScatterPostProcess::LightScatterPostProcess( ID3D11Device *pd3dDevice,
	UINT backBufferWidth, UINT backBufferHeight, UINT maxSamplesInSlice,
	UINT numEpipolarSlices, UINT initialSampleStepInSlice, float refinementThreshold,
	UINT epipoleSamplingDensityFactor, UINT lightType, UINT minMaxShadowMapResolution,
	UINT maxShadowMapStep ) :
	mLightScatterFX( 0 ),
	mRefineSampleLocationsFX( 0 ),
	mDisableDepthTestDS( 0 ),
	mDisableDepthTestIncrStencilDS( 0 ),
	mNoDepth_StEqual_IncrStencilDS( 0 ),
	mSolidFillNoCullRS( 0 ),
	mDefaultBS( 0 ),
	mCameraSpaceZRTV( 0 ),
	mCameraSpaceZSRV( 0 ),
	mSliceEndpointsRTV( 0 ),
	mSliceEndpointsSRV( 0 ),
	mCoordinateTextureRTV( 0 ),
	mCoordinateTextureSRV( 0 ),
	mEpipolarCamSpaceZRTV( 0 ),
	mEpipolarCamSpaceZSRV( 0 ),
	mEpipolarImageDSV( 0 ),
	mInterpolationSourceSRV( 0 ),
	mInterpolationSourceUAV( 0 ),
	mSliceUVDirAndOriginRTV( 0 ),
	mSliceUVDirAndOriginSRV( 0 ),
	mMaxSamplesInSlice( maxSamplesInSlice ),
	mNumEpipolarSlices( numEpipolarSlices ),
	mInitialSampleStepInSlice( initialSampleStepInSlice ),
	mRefinementThreshold( refinementThreshold ),
	mEpipoleSamplingDensityFactor( epipoleSamplingDensityFactor ),
	mLightType( lightType ),
	mMinMaxShadowMapResolution( minMaxShadowMapResolution ),
	mMaxShadowMapStep( maxShadowMapStep )
{
	for (int i = 0; i < 2; ++i)
	{
		mMinMaxShadowMapRTV[i] = 0;
		mMinMaxShadowMapSRV[i] = 0;
	}

	UINT sampleRefinementCSMinimumThreadGroupSize = 128; // Must be greater than 32
	// Thread group size must be at least as large as initial sample step.
	mSampleRefinementCSThreadGroupSize = max( sampleRefinementCSMinimumThreadGroupSize, initialSampleStepInSlice );
	// Thread group size cannot be larger than the total number of samples in slice.
	mSampleRefinementCSThreadGroupSize = min( mSampleRefinementCSThreadGroupSize, maxSamplesInSlice );

	CompileShader( pd3dDevice, "Shaders/LightScatter.fx", &mLightScatterFX );
	CompileShader( pd3dDevice, "Shaders/RefineSampleLocations.fx", &mRefineSampleLocationsFX );
	mReconstructCameraSpaceZTech = mLightScatterFX->GetTechniqueByName("ReconstructCameraSpaceZ");
	mGenerateSliceEndpointsTech = mLightScatterFX->GetTechniqueByName("GenerateSliceEndpoints");
	mRenderCoordinateTextureTech = mLightScatterFX->GetTechniqueByName("GenerateCoordinateTexture");
	mRefineSampleLocationsTech = mRefineSampleLocationsFX->GetTechniqueByName("RefineSampleLocations");
	mRenderSliceUVDirInSMTech = mLightScatterFX->GetTechniqueByName("RenderSliceUVDirection");
	mInitializeMinMaxShadowMapTech = mLightScatterFX->GetTechniqueByName("InitializeMinMaxShadowMap");
	mComputeMinMaxShadowMapLevelTech = mLightScatterFX->GetTechniqueByName("ComputeMinMaxShadowMapLevel");
	mMarkRayMarchingSamplesInStencilTech = mLightScatterFX->GetTechniqueByName("MarkRayMarchingSamplesInStencil");

	//
	// Create states
	//
	HRESULT hr;

	D3D11_DEPTH_STENCIL_DESC disableDepthTestDSDesc;
	ZeroMemory( &disableDepthTestDSDesc, sizeof(disableDepthTestDSDesc) );
	disableDepthTestDSDesc.DepthEnable = FALSE;
	disableDepthTestDSDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	V( pd3dDevice->CreateDepthStencilState( &disableDepthTestDSDesc, &mDisableDepthTestDS ) );

	// Disable depth testing and always increment stencil value.
	// This depth stencil state is used to mark samples which will undergo further
	// processing. Pixel shader discards pixels which should not be further
	// processed, thus keeping stencil value untouched. For instance, pixel shader
	// performing epipolar coordinate generation discards samples whose coordinates
	// are outside the screen [-1,1]x[-1,1] area.
	D3D11_DEPTH_STENCIL_DESC disableDepthIncrStencilDSDesc = disableDepthTestDSDesc;
	disableDepthIncrStencilDSDesc.StencilEnable = TRUE;
	disableDepthIncrStencilDSDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	disableDepthIncrStencilDSDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
	disableDepthIncrStencilDSDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	disableDepthIncrStencilDSDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	disableDepthIncrStencilDSDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	disableDepthIncrStencilDSDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_INCR;
	disableDepthIncrStencilDSDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	disableDepthIncrStencilDSDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
	disableDepthIncrStencilDSDesc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
	disableDepthIncrStencilDSDesc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
	V( pd3dDevice->CreateDepthStencilState( &disableDepthIncrStencilDSDesc, &mDisableDepthTestIncrStencilDS ) );

	// Disable depth testing, stencil testing function equal, increment stencil.
	// This state is used to process only these pixels that were marked at the
	// previous pass. All pixels with different stencil value are discarded from
	// further processing as well as some pixels can also be discarded during the
	// draw call. For instance, pixel shader marking ray marching samples processes
	// only these pixels which are inside the screen. It also discards all but
	// these samples which are interpolated from themselves.
	D3D11_DEPTH_STENCIL_DESC disableDepthStencilEqualIncrStencilDSDesc = disableDepthIncrStencilDSDesc;
	disableDepthStencilEqualIncrStencilDSDesc.FrontFace.StencilFunc = D3D11_COMPARISON_EQUAL;
	disableDepthStencilEqualIncrStencilDSDesc.BackFace.StencilFunc = D3D11_COMPARISON_EQUAL;
	V( pd3dDevice->CreateDepthStencilState( &disableDepthStencilEqualIncrStencilDSDesc, &mNoDepth_StEqual_IncrStencilDS ) );

	D3D11_RASTERIZER_DESC solidFillNoCullRSDesc;
	ZeroMemory( &solidFillNoCullRSDesc, sizeof(solidFillNoCullRSDesc) );
	solidFillNoCullRSDesc.FillMode = D3D11_FILL_SOLID;
	solidFillNoCullRSDesc.CullMode = D3D11_CULL_NONE;
	V( pd3dDevice->CreateRasterizerState( &solidFillNoCullRSDesc, &mSolidFillNoCullRS ) );

	D3D11_BLEND_DESC defaultBlendStateDesc;
	ZeroMemory( &defaultBlendStateDesc, sizeof(defaultBlendStateDesc) );
	defaultBlendStateDesc.IndependentBlendEnable = FALSE;
	for (int i = 0; i < 8; ++i)
		defaultBlendStateDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	V( pd3dDevice->CreateBlendState( &defaultBlendStateDesc, &mDefaultBS ) );

	CreateTextures( pd3dDevice );

	Resize( pd3dDevice, backBufferWidth, backBufferHeight );
}

LightScatterPostProcess::~LightScatterPostProcess( void )
{
	SAFE_RELEASE( mLightScatterFX );
	SAFE_RELEASE( mRefineSampleLocationsFX );

	SAFE_RELEASE( mDisableDepthTestDS );
	SAFE_RELEASE( mDisableDepthTestIncrStencilDS );
	SAFE_RELEASE( mNoDepth_StEqual_IncrStencilDS );
	SAFE_RELEASE( mSolidFillNoCullRS );
	SAFE_RELEASE( mDefaultBS );

	SAFE_RELEASE( mCameraSpaceZRTV );
	SAFE_RELEASE( mCameraSpaceZSRV );
	SAFE_RELEASE( mSliceEndpointsRTV );
	SAFE_RELEASE( mSliceEndpointsSRV );
	SAFE_RELEASE( mCoordinateTextureRTV );
	SAFE_RELEASE( mCoordinateTextureSRV );
	SAFE_RELEASE( mEpipolarCamSpaceZRTV );
	SAFE_RELEASE( mEpipolarCamSpaceZSRV );
	SAFE_RELEASE( mEpipolarImageDSV );
	SAFE_RELEASE( mInterpolationSourceSRV );
	SAFE_RELEASE( mInterpolationSourceUAV );
	SAFE_RELEASE( mSliceUVDirAndOriginRTV );
	SAFE_RELEASE( mSliceUVDirAndOriginSRV );
	for (int i = 0; i < 2; ++i)
	{
		SAFE_RELEASE( mMinMaxShadowMapRTV[i] );
		SAFE_RELEASE( mMinMaxShadowMapSRV[i] );
	}
}

void LightScatterPostProcess::Resize( ID3D11Device *pd3dDevice, UINT width, UINT height )
{
	mBackBufferWidth = width;
	mBackBufferHeight = height;

	HRESULT hr;

	D3D11_TEXTURE2D_DESC camSpaceZTexDesc = 
    {
        width,								//UINT Width;
        height,								//UINT Height;
        1,                                  //UINT MipLevels;
        1,                                  //UINT ArraySize;
        DXGI_FORMAT_R32_FLOAT,			    //DXGI_FORMAT Format;
        { 1 ,0 },                           //DXGI_SAMPLE_DESC SampleDesc;
        D3D11_USAGE_DEFAULT,                //D3D11_USAGE Usage;
        D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, //UINT BindFlags;
        0,                                  //UINT CPUAccessFlags;
        0,                                  //UINT MiscFlags;
    };

	SAFE_RELEASE( mCameraSpaceZRTV );
	SAFE_RELEASE( mCameraSpaceZSRV );

	ID3D11Texture2D *camSpaceZTex;
	V( pd3dDevice->CreateTexture2D( &camSpaceZTexDesc, NULL, &camSpaceZTex ) );
	V( pd3dDevice->CreateShaderResourceView( camSpaceZTex, NULL, &mCameraSpaceZSRV ) );
	V( pd3dDevice->CreateRenderTargetView( camSpaceZTex, NULL, &mCameraSpaceZRTV ) );

	SAFE_RELEASE( camSpaceZTex );
}

void LightScatterPostProcess::CreateTextures( ID3D11Device *pd3dDevice )
{
	HRESULT hr;

	D3D11_TEXTURE2D_DESC coordinateTexDesc =
	{
		mMaxSamplesInSlice,										// UINT Width;
		mNumEpipolarSlices,										// UINT Height;
		1,														// UINT MipLevels;
		1,														// UINT ArraySize;
		DXGI_FORMAT_R32G32_FLOAT,								// DXGI_FORMAT Format;
		{ 1, 0 },												// DXGI_SAMPLE_DESC SampleDesc;
		D3D11_USAGE_DEFAULT,									// D3D11_USAGE Usage;
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,	// UINT BindFlags;
		0,														// UINT CPUAccessFlags;
		0,														// UINT MiscFlags;
	};

	{
		ID3D11Texture2D *coordinateTexture;
		V( pd3dDevice->CreateTexture2D( &coordinateTexDesc, NULL, &coordinateTexture ) );
		V( pd3dDevice->CreateShaderResourceView( coordinateTexture, NULL, &mCoordinateTextureSRV ) );
		V( pd3dDevice->CreateRenderTargetView( coordinateTexture, NULL, &mCoordinateTextureRTV ) );
		SAFE_RELEASE( coordinateTexture );
	}

	{
		SAFE_RELEASE( mSliceEndpointsSRV );
		SAFE_RELEASE( mSliceEndpointsRTV );
		D3D11_TEXTURE2D_DESC interpolationSourceTexDesc = coordinateTexDesc;
		interpolationSourceTexDesc.Width = mNumEpipolarSlices;
		interpolationSourceTexDesc.Height = 1;
		interpolationSourceTexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		interpolationSourceTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		ID3D11Texture2D *sliceEndpoints;
		V( pd3dDevice->CreateTexture2D( &interpolationSourceTexDesc, NULL, &sliceEndpoints ) );
		V( pd3dDevice->CreateShaderResourceView( sliceEndpoints, NULL, &mSliceEndpointsSRV ) );
		V( pd3dDevice->CreateRenderTargetView( sliceEndpoints, NULL, &mSliceEndpointsRTV ) );
		SAFE_RELEASE( sliceEndpoints );
	}

	{
		SAFE_RELEASE( mInterpolationSourceSRV );
		SAFE_RELEASE( mInterpolationSourceUAV );
		D3D11_TEXTURE2D_DESC interpolationSourceTexDesc = coordinateTexDesc;
		interpolationSourceTexDesc.Format = DXGI_FORMAT_R16G16_UINT;
		interpolationSourceTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		ID3D11Texture2D *interpolationSource;
		V( pd3dDevice->CreateTexture2D( &interpolationSourceTexDesc, NULL, &interpolationSource ) );
		V( pd3dDevice->CreateShaderResourceView( interpolationSource, NULL, &mInterpolationSourceSRV ) );
		V( pd3dDevice->CreateUnorderedAccessView( interpolationSource, NULL, &mInterpolationSourceUAV ) );
		SAFE_RELEASE( interpolationSource );
	}
	
	{
		SAFE_RELEASE( mEpipolarCamSpaceZSRV );
		SAFE_RELEASE( mEpipolarCamSpaceZRTV );
		D3D11_TEXTURE2D_DESC epipolarCamSpaceZTexDesc = coordinateTexDesc;
		epipolarCamSpaceZTexDesc.Format = DXGI_FORMAT_R32_FLOAT;
		epipolarCamSpaceZTexDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
		ID3D11Texture2D *epipolarCamSpace;
		V( pd3dDevice->CreateTexture2D( &epipolarCamSpaceZTexDesc, NULL, &epipolarCamSpace ) );
		V( pd3dDevice->CreateShaderResourceView( epipolarCamSpace, NULL, &mEpipolarCamSpaceZSRV ) );
		V( pd3dDevice->CreateRenderTargetView( epipolarCamSpace, NULL, &mEpipolarCamSpaceZRTV ) );
		SAFE_RELEASE( epipolarCamSpace );
	}

	{
		// SCATTEREDLIGHT HERE
	}

	{
		SAFE_RELEASE( mEpipolarImageDSV );
		D3D11_TEXTURE2D_DESC epipolarDepthTexDesc = coordinateTexDesc;
		epipolarDepthTexDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		epipolarDepthTexDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		ID3D11Texture2D *epipolarImage;
		V( pd3dDevice->CreateTexture2D( &epipolarDepthTexDesc, NULL, &epipolarImage ) );
		V( pd3dDevice->CreateDepthStencilView( epipolarImage, NULL, &mEpipolarImageDSV ) );
		SAFE_RELEASE( epipolarImage );
	}

	{
		SAFE_RELEASE( mSliceUVDirAndOriginSRV );
		SAFE_RELEASE( mSliceUVDirAndOriginRTV );
		D3D11_TEXTURE2D_DESC sliceUVDirInShadowMapTexDesc = coordinateTexDesc;
		sliceUVDirInShadowMapTexDesc.Width = mNumEpipolarSlices;
		sliceUVDirInShadowMapTexDesc.Height = 1;
		sliceUVDirInShadowMapTexDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		ID3D11Texture2D *sliceUVDirInShadowMap;
		V( pd3dDevice->CreateTexture2D( &sliceUVDirInShadowMapTexDesc, NULL, &sliceUVDirInShadowMap ) );
		V( pd3dDevice->CreateShaderResourceView( sliceUVDirInShadowMap, NULL, &mSliceUVDirAndOriginSRV ) );
		V( pd3dDevice->CreateRenderTargetView( sliceUVDirInShadowMap, NULL, &mSliceUVDirAndOriginRTV ) );
		SAFE_RELEASE( sliceUVDirInShadowMap );
	}

	V( CreateMinMaxShadowMap( pd3dDevice ) );
}

HRESULT LightScatterPostProcess::CreateMinMaxShadowMap( ID3D11Device *pd3dDevice )
{
	D3D11_TEXTURE2D_DESC minMaxShadowMapTexDesc =
	{
		// Min/max shadow map does not contain finest resolution level of the
		// shadow map.
		mMinMaxShadowMapResolution,								// UINT Width;
		mNumEpipolarSlices,										// UINT Height;
		1,														// UINT MipLevels;
		1,														// UINT ArraySize;
		DXGI_FORMAT_R16G16_FLOAT,								// DXGI_FORMAT Format;
		{ 1, 0 },												// DXGI_SAMPLE_DESC SampleDesc;
		D3D11_USAGE_DEFAULT,									// D3D11_USAGE Usage;
		D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET,	// UINT BindFlags;
		0,														// UINT CPUAccessFlags;
		0,														// UINT MiscFlags;
	};

	HRESULT hr;

	for (int i = 0; i < 2; ++i)
	{
		SAFE_RELEASE( mMinMaxShadowMapSRV[i] );
		SAFE_RELEASE( mMinMaxShadowMapRTV[i] );

		ID3D11Texture2D *minMaxShadowMap;
		V_RETURN( pd3dDevice->CreateTexture2D( &minMaxShadowMapTexDesc, NULL, &minMaxShadowMap ) );
		V_RETURN( pd3dDevice->CreateShaderResourceView( minMaxShadowMap, NULL, &mMinMaxShadowMapSRV[i] ) );
		V_RETURN( pd3dDevice->CreateRenderTargetView( minMaxShadowMap, NULL, &mMinMaxShadowMapRTV[i] ) );
		SAFE_RELEASE( minMaxShadowMap );
	}

	return S_OK;
}

void LightScatterPostProcess::PerformLightScatter( ID3D11DeviceContext *pd3dDeviceContext,
	ID3D11ShaderResourceView *sceneDepth, CXMMATRIX cameraProj, XMFLOAT2 screenResolution,
	XMFLOAT4 lightScreenPos, CXMMATRIX viewProjInv, XMFLOAT4 cameraPos, XMFLOAT4 dirOnLight,
	XMFLOAT4 lightWorldPos, XMFLOAT4 spotLightAxisAndCosAngle, CXMMATRIX worldToLightProj,
	XMFLOAT4 cameraUVAndDepthInShadowMap, XMFLOAT2 shadowMapTexelSize, ID3D11ShaderResourceView *shadowMap )
{
	mRefineSampleLocationsFX->GetVariableByName("gLightScreenPos")->AsVector()->SetFloatVector((float*)&lightScreenPos);
	mRefineSampleLocationsFX->GetVariableByName("gMaxSamplesInSlice")->AsScalar()->SetInt(mMaxSamplesInSlice);

	mLightScatterFX->GetVariableByName("gCameraProj")->AsMatrix()->SetMatrix((float*)&cameraProj);
	mLightScatterFX->GetVariableByName("gNumEpipolarSlices")->AsScalar()->SetInt(mNumEpipolarSlices);
	mLightScatterFX->GetVariableByName("gScreenResolution")->AsVector()->SetFloatVector((float*)&screenResolution);
	mLightScatterFX->GetVariableByName("gLightScreenPos")->AsVector()->SetFloatVector((float*)&lightScreenPos);
	mLightScatterFX->GetVariableByName("gMaxSamplesInSlice")->AsScalar()->SetInt(mMaxSamplesInSlice);
	mLightScatterFX->GetVariableByName("gViewProjInv")->AsMatrix()->SetMatrix((float*)&viewProjInv);
	mLightScatterFX->GetVariableByName("gCameraPos")->AsVector()->SetFloatVector((float*)&cameraPos);
	mLightScatterFX->GetVariableByName("gDirOnLight")->AsVector()->SetFloatVector((float*)&dirOnLight);
	mLightScatterFX->GetVariableByName("gLightWorldPos")->AsVector()->SetFloatVector((float*)&lightWorldPos);
	mLightScatterFX->GetVariableByName("gSpotLightAxisAndCosAngle")->AsVector()->SetFloatVector((float*)&spotLightAxisAndCosAngle);
	mLightScatterFX->GetVariableByName("gWorldToLightProj")->AsMatrix()->SetMatrix((float*)&worldToLightProj);
	mLightScatterFX->GetVariableByName("gCameraUVAndDepthInShadowMap")->AsVector()->SetFloatVector((float*)&cameraUVAndDepthInShadowMap);
	mLightScatterFX->GetVariableByName("gShadowMapTexelSize")->AsVector()->SetFloatVector((float*)&shadowMapTexelSize);

	// Step 1: Reconstruct camera space Z (convert post projection to linear)
	// Requires: Scene depth, camera proj
	ReconstructCameraSpaceZ( pd3dDeviceContext, sceneDepth );

	// Pre step 2: Render slice end points. Stores entry and exit points of
	// epipolar lines for every slice.
	// Requires:
	RenderSliceEndpoints( pd3dDeviceContext );

	// Step 2: Render coordinate textures (epipolar coordinates, camera space z, and depth stencil)
	// Requires:
	RenderCoordinateTexture( pd3dDeviceContext );

	// Step 3: Refine sample locations (initial ray marching samples)
	RefineSampleLocations( pd3dDeviceContext );

	// Step 4: Render slice UV direction
	// [num slices x 1] texture containing slice directions in shadow map
	// UV space.
	RenderSliceUVDirection( pd3dDeviceContext );

	// Step 5: Build 1D min/max mipmap
	Build1DMinMaxMipMap( pd3dDeviceContext, shadowMap );

	// Step 6: Mark ray marching samples in stencil
	MarkRayMarchingSamples( pd3dDeviceContext );
}

void LightScatterPostProcess::ReconstructCameraSpaceZ( ID3D11DeviceContext *pd3dDeviceContext,
	ID3D11ShaderResourceView *sceneDepth )
{
	// Depth buffer is non-linear and cannot be interpolated directly.
	// In order to be able to use bilinear filtering, the camera space z must
	// be reconstructed.

	pd3dDeviceContext->OMSetRenderTargets( 1, &mCameraSpaceZRTV, NULL );
	pd3dDeviceContext->OMSetDepthStencilState( mDisableDepthTestDS, 0 );
	pd3dDeviceContext->RSSetState( mSolidFillNoCullRS );
	float blendFactor[] = { 0, 0, 0, 0 };
	pd3dDeviceContext->OMSetBlendState( mDefaultBS, blendFactor, 0xFFFFFFFF );

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = static_cast<float>( mBackBufferWidth );
	vp.Height = static_cast<float>( mBackBufferHeight );
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	pd3dDeviceContext->RSSetViewports( 1, &vp );

	mLightScatterFX->GetVariableByName("gSceneDepth")->AsShaderResource()->SetResource( sceneDepth );

	mReconstructCameraSpaceZTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
	pd3dDeviceContext->Draw( 3, 0 );

	// Unbind shader resources
	mLightScatterFX->GetVariableByName("gSceneDepth")->AsShaderResource()->SetResource( 0 );
	mReconstructCameraSpaceZTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );

	pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );
}

void LightScatterPostProcess::RenderSliceEndpoints( ID3D11DeviceContext *pd3dDeviceContext )
{
	pd3dDeviceContext->OMSetRenderTargets( 1, &mSliceEndpointsRTV, NULL );
	pd3dDeviceContext->OMSetDepthStencilState( mDisableDepthTestDS, 0 );
	pd3dDeviceContext->RSSetState( mSolidFillNoCullRS );
	float blendFactor[] = { 0, 0, 0, 0 };
	pd3dDeviceContext->OMSetBlendState( mDefaultBS, blendFactor, 0xFFFFFFFF );

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = static_cast<float>( mNumEpipolarSlices );
	vp.Height = static_cast<float>( 1 );
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	pd3dDeviceContext->RSSetViewports( 1, &vp );

	mGenerateSliceEndpointsTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
	pd3dDeviceContext->Draw( 3, 0 );

	pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );
}

void LightScatterPostProcess::RenderCoordinateTexture( ID3D11DeviceContext *pd3dDeviceContext )
{
	// Coordinate texture is a texture with dimensions [Total samples X Num slices]
	// Texel [i,j] contains projection space screen coordinates of the i-th sample
	// in the j-th epipolar slice.
	ID3D11RenderTargetView *rtvs[] = { mCoordinateTextureRTV, mEpipolarCamSpaceZRTV };
	pd3dDeviceContext->OMSetRenderTargets( 2, rtvs, mEpipolarImageDSV );
	pd3dDeviceContext->OMSetDepthStencilState( mDisableDepthTestIncrStencilDS, 0 );
	pd3dDeviceContext->RSSetState( mSolidFillNoCullRS );
	float blendFactor[] = { 0, 0, 0, 0 };
	pd3dDeviceContext->OMSetBlendState( mDefaultBS, blendFactor, 0xFFFFFFFF );

	static const float invalidCoordinate = -1e+10;
	float invalidCoords[] = { invalidCoordinate, invalidCoordinate, invalidCoordinate, invalidCoordinate };
	// Clear both render targets with values that can't be correct projection space
	// coordinates and camera space Z
	pd3dDeviceContext->ClearRenderTargetView( mCoordinateTextureRTV, invalidCoords );
	pd3dDeviceContext->ClearRenderTargetView( mEpipolarCamSpaceZRTV, invalidCoords );
	// Clear stencil to 0 (we use stencil part only)
	pd3dDeviceContext->ClearDepthStencilView( mEpipolarImageDSV, D3D11_CLEAR_STENCIL, 1.0f, 0 );

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = static_cast<float>( mMaxSamplesInSlice );
	vp.Height = static_cast<float>( mNumEpipolarSlices );
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	pd3dDeviceContext->RSSetViewports( 1, &vp );

	mLightScatterFX->GetVariableByName("gCamSpaceZ")->AsShaderResource()->SetResource( mCameraSpaceZSRV );
	mLightScatterFX->GetVariableByName("gSliceEndPoints")->AsShaderResource()->SetResource( mSliceEndpointsSRV );

	// Depth stencil state is configured to always increment stencil value. If
	// coordinates are outside the screen, the pixel shader discards the pixel
	// and stencil value is left untouched. All such pixels will be skipped from
	// further processing.
	mRenderCoordinateTextureTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
	pd3dDeviceContext->Draw( 3, 0 );

	// Unbind shader resources
	mLightScatterFX->GetVariableByName("gCamSpaceZ")->AsShaderResource()->SetResource( 0 );
	mLightScatterFX->GetVariableByName("gSliceEndPoints")->AsShaderResource()->SetResource( 0 );
	mRenderCoordinateTextureTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );

	pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );
}

void LightScatterPostProcess::RefineSampleLocations( ID3D11DeviceContext *pd3dDeviceContext )
{
	mRefineSampleLocationsFX->GetVariableByName("gCoordinates")->AsShaderResource()->SetResource( mCoordinateTextureSRV );
	mRefineSampleLocationsFX->GetVariableByName("gEpipolarCamSpaceZ")->AsShaderResource()->SetResource( mEpipolarCamSpaceZSRV );
	mRefineSampleLocationsFX->GetVariableByName("gInterpolationSource")->AsUnorderedAccessView()->SetUnorderedAccessView( mInterpolationSourceUAV );
	mRefineSampleLocationsFX->GetTechniqueByName("RefineSampleLocations")->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
	
	pd3dDeviceContext->Dispatch( mMaxSamplesInSlice / mSampleRefinementCSThreadGroupSize,
								 mNumEpipolarSlices,
								 1 );

	mRefineSampleLocationsFX->GetVariableByName("gCoordinates")->AsShaderResource()->SetResource( 0 );
	mRefineSampleLocationsFX->GetVariableByName("gEpipolarCamSpaceZ")->AsShaderResource()->SetResource( 0 );
	mRefineSampleLocationsFX->GetVariableByName("gInterpolationSource")->AsUnorderedAccessView()->SetUnorderedAccessView( 0 );
	mRefineSampleLocationsFX->GetTechniqueByName("RefineSampleLocations")->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
}

void LightScatterPostProcess::RenderSliceUVDirection( ID3D11DeviceContext *pd3dDeviceContext )
{
	// Render [num slices x 1] texture containing slice direction in shadow map
	// UV space.
	pd3dDeviceContext->OMSetRenderTargets( 1, &mSliceUVDirAndOriginRTV, NULL );
	pd3dDeviceContext->OMSetDepthStencilState( mDisableDepthTestDS, 0 );
	pd3dDeviceContext->RSSetState( mSolidFillNoCullRS );
	float blendFactor[] = { 0, 0, 0, 0 };
	pd3dDeviceContext->OMSetBlendState( mDefaultBS, blendFactor, 0xFFFFFFFF );

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = static_cast<float>( mNumEpipolarSlices );
	vp.Height = static_cast<float>( 1 );
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	pd3dDeviceContext->RSSetViewports( 1, &vp );

	mLightScatterFX->GetVariableByName("gCamSpaceZ")->AsShaderResource()->SetResource( mCameraSpaceZSRV );
	mLightScatterFX->GetVariableByName("gSliceEndPoints")->AsShaderResource()->SetResource( mSliceEndpointsSRV );

	mRenderSliceUVDirInSMTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
	pd3dDeviceContext->Draw( 3, 0 );
	
	// Unbind shader resources
	mLightScatterFX->GetVariableByName("gCamSpaceZ")->AsShaderResource()->SetResource( 0 );
	mLightScatterFX->GetVariableByName("gSliceEndPoints")->AsShaderResource()->SetResource( 0 );
	mRenderSliceUVDirInSMTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );

	pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );
}

void LightScatterPostProcess::Build1DMinMaxMipMap( ID3D11DeviceContext *pd3dDeviceContext,
												  ID3D11ShaderResourceView *shadowMap )
{
	pd3dDeviceContext->OMSetDepthStencilState( mDisableDepthTestDS, 0 );
	pd3dDeviceContext->RSSetState( mSolidFillNoCullRS );
	float blendFactor[] = { 0, 0, 0, 0 };
	pd3dDeviceContext->OMSetBlendState( mDefaultBS, blendFactor, 0xFFFFFFFF );

	UINT xOffset = 0;
	UINT prevXOffset = 0;
	UINT parity = 0;
	ID3D11Resource *minMaxShadowMap0, *minMaxShadowMap1;
	mMinMaxShadowMapRTV[0]->GetResource( &minMaxShadowMap0 );
	mMinMaxShadowMapRTV[1]->GetResource( &minMaxShadowMap1 );
	
	// Note that we start rendering min/max shadow map from step 2.
	for (UINT step = 2; step <= mMaxShadowMapStep; step *= 2, parity = (parity+1)%2 )
	{
		// Use two buffers which are in turn used as the source and destination.
		pd3dDeviceContext->OMSetRenderTargets( 1, &mMinMaxShadowMapRTV[parity], NULL );

		if (step == 2)
		{
			// At the initial pass, the shader gathers 8 depths which will be used
			// for PCF filtering at the sample location and its next neighbor along
			// the slice and outputs min/max depths.

			mLightScatterFX->GetVariableByName("gSliceUVDirAndOrigin")->AsShaderResource()->SetResource( mSliceUVDirAndOriginSRV );
			mLightScatterFX->GetVariableByName("gLightSpaceDepthMap")->AsShaderResource()->SetResource( shadowMap );
		}
		else
		{
			// At the subsequent passes, the shader loads two min/max values from
			// the next finer level to compute next level of the binary tree.

			// Set source and destination min/max data offsets:
			mLightScatterFX->GetVariableByName("gSrcMinMaxLevelXOffset")->AsScalar()->SetInt( prevXOffset );
			mLightScatterFX->GetVariableByName("gDstMinMaxLevelXOffset")->AsScalar()->SetInt( xOffset );
			// HEST: Y?
			mLightScatterFX->GetVariableByName("gMinMaxLightSpaceDepth")->AsShaderResource()->SetResource( mMinMaxShadowMapSRV[ (parity+1)%2 ] );
		}

		D3D11_VIEWPORT vp;
		vp.TopLeftX = xOffset;
		vp.TopLeftY = 0;
		vp.Width = static_cast<float>( mMinMaxShadowMapResolution ) / step;
		vp.Height = static_cast<float>( mNumEpipolarSlices );
		vp.MinDepth = 0;
		vp.MaxDepth = 1;
		pd3dDeviceContext->RSSetViewports( 1, &vp );

		if (step > 2)
			mComputeMinMaxShadowMapLevelTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
		else
			mInitializeMinMaxShadowMapTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
		
		pd3dDeviceContext->Draw( 3, 0 );

		// Unbind resources
		if (step == 2)
		{
			mLightScatterFX->GetVariableByName("gSliceUVDirAndOrigin")->AsShaderResource()->SetResource( 0 );
			mLightScatterFX->GetVariableByName("gLightSpaceDepthMap")->AsShaderResource()->SetResource( 0 );
			mInitializeMinMaxShadowMapTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
		}
		else
		{
			mLightScatterFX->GetVariableByName("gMinMaxLightSpaceDepth")->AsShaderResource()->SetResource( 0 );
			mComputeMinMaxShadowMapLevelTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
		}

		// All the data must reside in 0-th texture, so copy current level, if
		// necessary, from 1-st texture.
		if (parity == 1)
		{
			D3D11_BOX srcBox;
			srcBox.left = xOffset;
			srcBox.right = xOffset + mMinMaxShadowMapResolution / step;
			srcBox.top = 0;
			srcBox.bottom = mNumEpipolarSlices;
			srcBox.front = 0;
			srcBox.back = 1;
			pd3dDeviceContext->CopySubresourceRegion( minMaxShadowMap0, 0, xOffset,
													0, 0, minMaxShadowMap1, 0, &srcBox );
		}

		prevXOffset = xOffset;
		xOffset += mMinMaxShadowMapResolution / step;
	}

	SAFE_RELEASE( minMaxShadowMap0 );
	SAFE_RELEASE( minMaxShadowMap1 );

	pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );
}

void LightScatterPostProcess::MarkRayMarchingSamples( ID3D11DeviceContext *pd3dDeviceContext )
{
	pd3dDeviceContext->OMSetDepthStencilState( mNoDepth_StEqual_IncrStencilDS, 1 );
	pd3dDeviceContext->RSSetState( mSolidFillNoCullRS );
	float blendFactor[] = { 0, 0, 0, 0 };
	pd3dDeviceContext->OMSetBlendState( mDefaultBS, blendFactor, 0xFFFFFFFF );

	D3D11_VIEWPORT vp;
	vp.TopLeftX = 0;
	vp.TopLeftY = 0;
	vp.Width = static_cast<float>( mMaxSamplesInSlice );
	vp.Height = static_cast<float>( mNumEpipolarSlices );
	vp.MinDepth = 0;
	vp.MaxDepth = 1;
	pd3dDeviceContext->RSSetViewports( 1, &vp );
	
	// Mark ray marching samples in the stencil.
	// The depth stencil state is configured to pass only pixels whose stencil
	// value equals 1. Thus all epipolar samples with coordinates outside of the
	// screen (generated in the previous pass) are automatically discarded. The pixel
	// shader only passes samples which are interpolated from themselves, the rest
	// are discarded. Thus after this pass all ray marching samples will be marked
	// with 2 in stencil.
	ID3D11RenderTargetView *nullRTV = NULL;
	pd3dDeviceContext->OMSetRenderTargets( 1, &nullRTV, mEpipolarImageDSV );
	mLightScatterFX->GetVariableByName("gInterpolationSource")->AsShaderResource()->SetResource( mInterpolationSourceSRV );

	mMarkRayMarchingSamplesInStencilTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );
	pd3dDeviceContext->Draw( 3, 0 );
	
	// Unbind shader resources
	mLightScatterFX->GetVariableByName("gInterpolationSource")->AsShaderResource()->SetResource( 0 );
	mMarkRayMarchingSamplesInStencilTech->GetPassByIndex( 0 )->Apply( 0, pd3dDeviceContext );

	pd3dDeviceContext->OMSetRenderTargets( 0, NULL, NULL );
}

void LightScatterPostProcess::CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx )
{
	DWORD shaderFlags = D3D10_SHADER_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
	shaderFlags |= D3D10_SHADER_DEBUG;
#endif

	ID3D10Blob *compiledShader;
	ID3D10Blob *errorMsgs;
	HRESULT hr;

	std::vector<D3D10_SHADER_MACRO> shaderMacros;
	std::stringstream ss;
	ss << mLightType;
	std::string def = ss.str();
	D3D10_SHADER_MACRO macro = { "LIGHT_TYPE", def.c_str() };
	shaderMacros.push_back( macro );

	if (filename == "Shaders/RefineSampleLocations.fx")
	{
		std::stringstream ss1, ss2;
		std::string def1, def2;
		ss1 << mInitialSampleStepInSlice;
		ss2 << mSampleRefinementCSThreadGroupSize;
		def1 = ss1.str();
		def2 = ss2.str();

		D3D10_SHADER_MACRO macros[3] = { "INITIAL_SAMPLE_STEP", def1.c_str(), "THREAD_GROUP_SIZE", def2.c_str(), NULL, NULL };
		shaderMacros.push_back( macros[0] );
		shaderMacros.push_back( macros[1] );
		shaderMacros.push_back( macros[2] );

		hr = D3DX11CompileFromFileA( filename, shaderMacros.data(), 0, "", "fx_5_0", shaderFlags, 0, 0,
			&compiledShader, &errorMsgs, 0 );
	}
	else
	{
		D3D10_SHADER_MACRO macros[1] = { NULL, NULL };
		shaderMacros.push_back( macros[0] );

		hr = D3DX11CompileFromFileA( filename, shaderMacros.data(), 0, "", "fx_5_0", shaderFlags, 0, 0,
			&compiledShader, &errorMsgs, 0 );
	}

	

	// errorMsgs can store errors or warnings.
	if (errorMsgs)
	{
		MessageBoxA( 0, (char*)errorMsgs->GetBufferPointer(), "D3DX11CompileFromFile", MB_OK );
		SAFE_RELEASE( errorMsgs );
	}

	// Even if there are no error messages, check to make sure there were no other errors.
	if (FAILED(hr))
	{
		DXTraceA( __FILE__, __LINE__, hr, "D3DX11CompileFromFile", true );
	}

	hr = D3DX11CreateEffectFromMemory( compiledShader->GetBufferPointer(),
		compiledShader->GetBufferSize(), 0, pd3dDevice, fx );
	if (FAILED(hr))
	{
		DXTraceA( __FILE__, __LINE__, hr, "D3DX11CreateEffectFromMemory", true );
	}

	// Done with compiled shader.
	SAFE_RELEASE( compiledShader );
}