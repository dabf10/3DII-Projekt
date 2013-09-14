#ifndef _SSAO_H_
#define _SSAO_H_

#include "DXUT.h"
#include "d3dx11effect.h"
#include <xnamath.h>

class SSAO
{
public:
	SSAO( ID3D11Device *pd3dDevice, UINT width, UINT height, float offset,
		float aoStart, float hemisphereRadius );
	~SSAO();

	// Calculates ambient occlusion according to the provided depth- and normal data.
	// The ambient occlusion data can be aquired using the AOMap() method.
	void CalculateSSAO( ID3D11ShaderResourceView *depthMap, ID3D11ShaderResourceView *normalMap,
		const XMMATRIX &proj, ID3D11DeviceContext *pd3dImmediateContext );
	
	ID3D11ShaderResourceView *AOMap() const { return mAOMapSRV; }

private:
	SSAO( const SSAO& rhs );
	SSAO &operator=( const SSAO& rhs );

	void CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx );
	void Blur( ID3D11ShaderResourceView *inputSRV, ID3D11RenderTargetView *outputRTV,
		bool horizontalBlur, ID3D11ShaderResourceView *depthMap, ID3D11ShaderResourceView *normalMap,
		ID3D11DeviceContext *pd3dImmediateContext );

private:
	float mOffset;
	float mAOStart;
	float mHemisphereRadius;

	ID3D11ShaderResourceView *mAOMapSRV;
	ID3D11ShaderResourceView *mIntermediateBlurSRV;
	ID3D11RenderTargetView *mAOMapRT;
	ID3D11RenderTargetView *mIntermediateBlurRT;
	ID3D11ShaderResourceView *mRandomNormalsSRV;
	
	ID3DX11Effect *mSSAOFX;
	ID3DX11Effect *mBilateralBlurFX;

	D3D11_VIEWPORT mViewport;
};

#endif // _SSAO_H_