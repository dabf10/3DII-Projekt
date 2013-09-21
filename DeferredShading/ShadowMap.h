#ifndef _SHADOWMAP_H_
#define _SHADOWMAP_H_

#include "DXUT.h"

class ShadowMap
{
public:
	ShadowMap( ID3D11Device *device, UINT resolution );
	~ShadowMap( );

	ID3D11ShaderResourceView *DepthMapSRV( );

	UINT Resolution( ) const { return mResolution; }

	void BindDsvAndSetNullRenderTarget( ID3D11DeviceContext *context );

private:
	ShadowMap( const ShadowMap& rhs );
	ShadowMap& operator=( const ShadowMap& rhs );

private:
	UINT mResolution;

	ID3D11ShaderResourceView *mDepthMapSRV;
	ID3D11DepthStencilView *mDepthMapDSV;

	D3D11_VIEWPORT mViewport;
};

#endif // _SHADOWMAP_H_