#ifndef _POSTPROCESSRT_H_
#define _POSTPROCESSRT_H_

#include <DXUT.h>

class PostProcessRT
{
public:
	PostProcessRT( );
	~PostProcessRT( );

	HRESULT Init( ID3D11Device *pd3dDevice, UINT width, UINT height );

	void Flip( );
	ID3D11RenderTargetView * const GetRTV( ) const { return mRTV[mCurRTV]; }
	ID3D11ShaderResourceView * const GetSRV( ) const { return mSRV[mCurSRV]; }

private:
	PostProcessRT( const PostProcessRT &rhs );
	PostProcessRT &operator=( const PostProcessRT &rhs );

private:
	ID3D11RenderTargetView *mRTV[2];
	ID3D11ShaderResourceView *mSRV[2];
	UINT mCurRTV;
	UINT mCurSRV;
};

#endif // _POSTPROCESSRT_H_