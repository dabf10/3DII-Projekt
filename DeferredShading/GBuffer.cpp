#include "GBuffer.h"

GBuffer::GBuffer( ID3D11Device *pd3dDevice, UINT width, UINT height ) :
	mWidth( width ),
	mHeight( height ),
	mColorSRV( 0 ),
	mColorRT( 0 ),
	mNormalSRV( 0 ),
	mNormalRT( 0 ),
	mClearGBufferFX( 0 )
{
	CompileShader( pd3dDevice, "Shaders/ClearGBuffer.fx", &mClearGBufferFX );

	mNumBuffers = 2;

	HRESULT hr;

	ID3D11Texture2D *tex;
	D3D11_TEXTURE2D_DESC texDesc;
	texDesc.ArraySize = 1;
	texDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	texDesc.CPUAccessFlags = 0;
	texDesc.Height = height;
	texDesc.Width = width;
	texDesc.MipLevels = 1;
	texDesc.MiscFlags = 0;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Usage = D3D11_USAGE_DEFAULT;

	// Color buffer
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V( pd3dDevice->CreateTexture2D( &texDesc, 0, &tex ) );
	V( pd3dDevice->CreateShaderResourceView( tex, NULL, &mColorSRV ) );
	V( pd3dDevice->CreateRenderTargetView( tex, NULL, &mColorRT ) );
	// Views saves reference
	SAFE_RELEASE( tex );

	// Normal buffer
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V( pd3dDevice->CreateTexture2D( &texDesc, 0, &tex ) );
	V( pd3dDevice->CreateShaderResourceView( tex, NULL, &mNormalSRV ) );
	V( pd3dDevice->CreateRenderTargetView( tex, NULL, &mNormalRT ) );
	// Views saves reference
	SAFE_RELEASE( tex );
}

GBuffer::~GBuffer( )
{
	SAFE_RELEASE( mClearGBufferFX );
	SAFE_RELEASE( mColorSRV );
	SAFE_RELEASE( mColorRT );
	SAFE_RELEASE( mNormalSRV );
	SAFE_RELEASE( mNormalRT );
}

void GBuffer::CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx )
{
	DWORD shaderFlags = D3D10_SHADER_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
	shaderFlags |= D3D10_SHADER_DEBUG;
#endif

	ID3D10Blob *compiledShader;
	ID3D10Blob *errorMsgs;
	HRESULT hr;

	hr = D3DX11CompileFromFileA( filename, 0, 0, "", "fx_5_0", shaderFlags, 0, 0,
		&compiledShader, &errorMsgs, 0 );

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

// Render a full-screen quad using a shader that clears the GBuffer to defaults.
// Assumes render targets are correctly set.
void GBuffer::Clear( ID3D11DeviceContext *pd3dImmediateContext )
{
	mClearGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	pd3dImmediateContext->Draw( 3, 0 );
}