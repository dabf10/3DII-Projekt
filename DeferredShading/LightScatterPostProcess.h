#ifndef _LIGHT_SCATTER_POST_PROCESS_H_
#define _LIGHT_SCATTER_POST_PROCESS_H_

#include "DXUT.h"
#include "d3dx11effect.h"
#include <xnamath.h>

class LightScatterPostProcess
{
public:
	LightScatterPostProcess( ID3D11Device *pd3dDevice, UINT backBufferWidth,
		UINT backBufferHeight, UINT maxSamplesInSlice, UINT numEpipolarSlices,
		UINT initialSampleStepInSlice, float refinementThreshold,
		UINT epipoleSamplingDensityFactor, UINT lightType, UINT minMaxShadowMapResolution,
		UINT maxShadowMapStep );
	~LightScatterPostProcess( void );

	void PerformLightScatter( ID3D11DeviceContext *pd3dDeviceContext,
		ID3D11ShaderResourceView *sceneDepth, CXMMATRIX cameraProj,
		XMFLOAT2 screenResolution, XMFLOAT4 lightScreenPos, CXMMATRIX viewProjInv,
		XMFLOAT4 cameraPos, XMFLOAT4 dirOnLight, XMFLOAT4 lightWorldPos,
		XMFLOAT4 spotLightAxisAndCosAngle, CXMMATRIX worldToLightProj,
		XMFLOAT4 cameraUVAndDepthInShadowMap, XMFLOAT2 shadowMapTexelSize,
		ID3D11ShaderResourceView *shadowMap );

	void Resize( ID3D11Device *pd3dDevice, UINT width, UINT height );

	ID3D11ShaderResourceView *CameraSpaceZ( void ) const { return mCameraSpaceZSRV; }
	ID3D11ShaderResourceView *SliceEndpoints( void ) const { return mSliceEndpointsSRV; }
	ID3D11ShaderResourceView *CoordinateTexture( void ) const { return mCoordinateTextureSRV; }
	ID3D11ShaderResourceView *EpipolarCamSpaceZ( void ) const { return mEpipolarCamSpaceZSRV; }
	// InterpolationSource är knepig att verifiera, det verkar inte som att man kan rendera innehållet?
	ID3D11ShaderResourceView *InterpolationSource( void ) const { return mInterpolationSourceSRV; }
	ID3D11ShaderResourceView *SliceUVDirAndOrigin( void ) const { return mSliceUVDirAndOriginSRV; }
	ID3D11ShaderResourceView *MinMaxShadowMap0( void ) const { return mMinMaxShadowMapSRV[0]; }
	ID3D11ShaderResourceView *MinMaxShadowMap1( void ) const { return mMinMaxShadowMapSRV[1]; }

private:
	LightScatterPostProcess &operator=( const LightScatterPostProcess &rhs );
	LightScatterPostProcess( const LightScatterPostProcess &rhs );

	void ReconstructCameraSpaceZ( ID3D11DeviceContext *pd3dDeviceContext,
		ID3D11ShaderResourceView *sceneDepth );
	void RenderSliceEndpoints( ID3D11DeviceContext *pd3dDeviceContext );
	void RenderCoordinateTexture( ID3D11DeviceContext *pd3dDeviceContext );
	void RefineSampleLocations( ID3D11DeviceContext *pd3dDeviceContext );
	void RenderSliceUVDirection( ID3D11DeviceContext *pd3dDeviceContext );
	void Build1DMinMaxMipMap( ID3D11DeviceContext *pd3dDeviceContext, ID3D11ShaderResourceView *shadowMap );
	HRESULT CreateMinMaxShadowMap( ID3D11Device *pd3dDevice );
	void MarkRayMarchingSamples( ID3D11DeviceContext *pd3dDeviceContext );

	void CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx );
	void CreateTextures( ID3D11Device *pd3dDevice );

private:
	UINT mBackBufferWidth;
	UINT mBackBufferHeight;
	UINT mMaxSamplesInSlice;
	UINT mNumEpipolarSlices;
	UINT mInitialSampleStepInSlice; // HEST: Inte säker på om den används mycket.
	UINT mSampleRefinementCSThreadGroupSize;
	float mRefinementThreshold;
	UINT mEpipoleSamplingDensityFactor;
	UINT mLightType;
	UINT mMinMaxShadowMapResolution;
	UINT mMaxShadowMapStep;

	// Effect and techniques
	ID3DX11Effect *mLightScatterFX;
	ID3DX11EffectTechnique *mReconstructCameraSpaceZTech;
	ID3DX11EffectTechnique *mGenerateSliceEndpointsTech;
	ID3DX11EffectTechnique *mRenderCoordinateTextureTech;
	ID3DX11Effect *mRefineSampleLocationsFX;
	ID3DX11EffectTechnique *mRefineSampleLocationsTech;
	ID3DX11EffectTechnique *mRenderSliceUVDirInSMTech;
	ID3DX11EffectTechnique *mInitializeMinMaxShadowMapTech;
	ID3DX11EffectTechnique *mComputeMinMaxShadowMapLevelTech;
	ID3DX11EffectTechnique *mMarkRayMarchingSamplesInStencilTech;

	// States
	ID3D11DepthStencilState *mDisableDepthTestDS;
	ID3D11DepthStencilState *mDisableDepthTestIncrStencilDS;
	ID3D11DepthStencilState *mNoDepth_StEqual_IncrStencilDS;
	ID3D11RasterizerState *mSolidFillNoCullRS;
	ID3D11BlendState *mDefaultBS;

	// Views
	ID3D11RenderTargetView *mCameraSpaceZRTV;
	ID3D11ShaderResourceView *mCameraSpaceZSRV;
	ID3D11RenderTargetView *mSliceEndpointsRTV;
	ID3D11ShaderResourceView *mSliceEndpointsSRV;
	ID3D11RenderTargetView *mCoordinateTextureRTV;
	ID3D11ShaderResourceView *mCoordinateTextureSRV;
	ID3D11RenderTargetView *mEpipolarCamSpaceZRTV;
	ID3D11ShaderResourceView *mEpipolarCamSpaceZSRV;
	ID3D11DepthStencilView *mEpipolarImageDSV;
	ID3D11ShaderResourceView *mInterpolationSourceSRV;
	ID3D11UnorderedAccessView *mInterpolationSourceUAV;
	ID3D11RenderTargetView *mSliceUVDirAndOriginRTV;
	ID3D11ShaderResourceView *mSliceUVDirAndOriginSRV;
	ID3D11RenderTargetView *mMinMaxShadowMapRTV[2];
	ID3D11ShaderResourceView *mMinMaxShadowMapSRV[2];
};

#endif // _LIGHT_SCATTER_POST_PROCESS_H_