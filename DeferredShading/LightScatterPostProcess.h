#ifndef _LIGHT_SCATTER_POST_PROCESS_H_
#define _LIGHT_SCATTER_POST_PROCESS_H_

#include "DXUT.h"
#include "d3dx11effect.h"
#include <xnamath.h>

class LightScatterPostProcess
{
public:
	LightScatterPostProcess( ID3D11Device *pd3dDevice, UINT backBufferWidth,
		UINT backBufferHeight, UINT maxSamplesInSlice, UINT numEpipolarSlices );
	~LightScatterPostProcess( void );

	void PerformLightScatter( ID3D11DeviceContext *pd3dDeviceContext,
		ID3D11ShaderResourceView *sceneDepth, CXMMATRIX cameraProj,
		XMFLOAT2 screenResolution, XMFLOAT4 lightScreenPos );

	void Resize( ID3D11Device *pd3dDevice, UINT width, UINT height );

	ID3D11ShaderResourceView *CameraSpaceZ( void ) const { return mCameraSpaceZSRV; }
	ID3D11ShaderResourceView *SliceEndpoints( void ) const { return mSliceEndpointsSRV; }

private:
	LightScatterPostProcess &operator=( const LightScatterPostProcess &rhs );
	LightScatterPostProcess( const LightScatterPostProcess &rhs );

	void ReconstructCameraSpaceZ( ID3D11DeviceContext *pd3dDeviceContext,
		ID3D11ShaderResourceView *sceneDepth );
	void RenderSliceEndpoints( ID3D11DeviceContext *pd3dDeviceContext );
	void RenderCoordinateTexture( ID3D11DeviceContext *pd3dDeviceContext );

	void CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx );
	void CreateTextures( ID3D11Device *pd3dDevice );

private:
	UINT mBackBufferWidth;
	UINT mBackBufferHeight;
	UINT mMaxSamplesInSlice;
	UINT mNumEpipolarSlices;

	// Effect and techniques
	ID3DX11Effect *mLightScatterFX;
	ID3DX11EffectTechnique *mReconstructCameraSpaceZTech;
	ID3DX11EffectTechnique *mGenerateSliceEndpointsTech;
	ID3DX11EffectTechnique *mRenderCoordinateTextureTech;

	// States
	ID3D11DepthStencilState *mDisableDepthTestDS;
	ID3D11DepthStencilState *mDisableDepthTestIncrStencilDS;
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
};

#endif // _LIGHT_SCATTER_POST_PROCESS_H_