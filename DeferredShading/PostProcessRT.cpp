#include "PostProcessRT.h"

PostProcessRT::PostProcessRT( ) : mCurRTV( 0 ), mCurSRV( 1 )
{
}

PostProcessRT::~PostProcessRT( )
{
	for (int i = 0; i < 2; ++i)
	{
		SAFE_RELEASE( mRTV[i] );
		SAFE_RELEASE( mSRV[i] );
	}
}

HRESULT PostProcessRT::Init( ID3D11Device *pd3dDevice, UINT width, UINT height )
{
	HRESULT hr;

	ID3D11Texture2D *tex;
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.ArraySize = 1;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.Width = width;
	texDesc.Height = height;
	texDesc.MipLevels = 1;
	texDesc.MiscFlags = 0;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

	for (int i = 0; i < 2; ++i)
	{
		V_RETURN( pd3dDevice->CreateTexture2D( &texDesc, 0, &tex ) );
		V_RETURN( pd3dDevice->CreateRenderTargetView( tex, NULL, &mRTV[i] ) );
		V_RETURN( pd3dDevice->CreateShaderResourceView( tex, NULL, &mSRV[i] ) );
		SAFE_RELEASE( tex );
	}
}

void PostProcessRT::Flip( )
{
	UINT temp = mCurRTV;
	mCurRTV = mCurSRV;
	mCurSRV = temp;
}