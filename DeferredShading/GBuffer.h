#ifndef _GBUFFER_H_
#define _GBUFFER_H_

#include "DXUT.h"
#include "d3dx11effect.h"

class GBuffer
{
public:
	GBuffer( ID3D11Device *pd3dDevice, UINT width, UINT height );
	~GBuffer();

	void Clear( ID3D11DeviceContext *pd3dImmediateContext );
	UINT NumBuffers( ) const { return mNumBuffers; }
	ID3D11ShaderResourceView *ColorSRV( ) const { return mColorSRV; }
	ID3D11RenderTargetView *ColorRT( ) const { return mColorRT; }
	ID3D11ShaderResourceView *NormalSRV( ) const { return mNormalSRV; }
	ID3D11RenderTargetView *NormalRT( ) const { return mNormalRT; }

private:
	GBuffer( const GBuffer& rhs );
	GBuffer &operator=( const GBuffer& rhs );

	void CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx );

private:
	UINT mWidth;
	UINT mHeight;
	UINT mNumBuffers;

	ID3D11RenderTargetView *mColorRT;
	ID3D11ShaderResourceView *mColorSRV;
	ID3D11RenderTargetView *mNormalRT;
	ID3D11ShaderResourceView *mNormalSRV;

	ID3DX11Effect *mClearGBufferFX;
};

#endif // _GBUFFER_H_