#include "SSAO.h"

SSAO::SSAO( ID3D11Device *pd3dDevice, UINT width, UINT height, float offset,
	float aoStart, float hemisphereRadius ) :
		mOffset(offset),
		mAOStart(aoStart),
		mHemisphereRadius(hemisphereRadius),
		mRandomNormalsSRV( 0 ),
		mAOMapSRV( 0 ),
		mAOMapRT( 0 ),
		mIntermediateBlurSRV( 0 ),
		mIntermediateBlurRT( 0 ),
		mSSAOFX( 0 ),
		mBilateralBlurFX( 0 )
{
	// Set up the viewport. Normally we calculate SSAO in a quarter of the back
	// buffer size, which is specified in the width and height parameters on
	// instansiation of this class.
	mViewport.TopLeftX = 0.0f;
	mViewport.TopLeftY = 0.0f;
	mViewport.Width = static_cast<float>( width );
	mViewport.Height = static_cast<float>( height );
	mViewport.MinDepth = 0.0f;
	mViewport.MaxDepth = 1.0f;

	CompileShader( pd3dDevice, "Shaders/SSAO.fx", &mSSAOFX );
	CompileShader( pd3dDevice, "Shaders/BilateralBlur.fx", &mBilateralBlurFX );
	
	// Load the random normals texture used in calculating SSAO.
	HRESULT hr;
	V( D3DX11CreateShaderResourceViewFromFileA( pd3dDevice, "noise.png", 0, 0,
		&mRandomNormalsSRV, 0 ) );

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
	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	ID3D11Texture2D *tex;

	// Create the ambient occlusion map SRV and RT.
	V( pd3dDevice->CreateTexture2D( &texDesc, 0, &tex ) );
	V( pd3dDevice->CreateShaderResourceView( tex, NULL, &mAOMapSRV ) );
	V( pd3dDevice->CreateRenderTargetView( tex, NULL, &mAOMapRT ) );
	// Views saves reference
	SAFE_RELEASE( tex );

	// Create the intermediate blur SRV and RT.
	V( pd3dDevice->CreateTexture2D( &texDesc, 0, &tex ) );
	V( pd3dDevice->CreateShaderResourceView( tex, NULL, &mIntermediateBlurSRV ) );
	V( pd3dDevice->CreateRenderTargetView( tex, NULL, &mIntermediateBlurRT ) );
	// Views saves reference
	SAFE_RELEASE( tex );
}

SSAO::~SSAO( )
{
	SAFE_RELEASE( mSSAOFX );
	SAFE_RELEASE( mBilateralBlurFX );
	SAFE_RELEASE( mRandomNormalsSRV );
	SAFE_RELEASE( mAOMapSRV );
	SAFE_RELEASE( mAOMapRT );
	SAFE_RELEASE( mIntermediateBlurSRV );
	SAFE_RELEASE( mIntermediateBlurRT );
}

void SSAO::CalculateSSAO( ID3D11ShaderResourceView *depthMap, ID3D11ShaderResourceView *normalMap,
	const XMMATRIX &proj, ID3D11DeviceContext *pd3dImmediateContext )
{
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pd3dImmediateContext->OMSetRenderTargets( 1, &mAOMapRT, 0 );
	pd3dImmediateContext->ClearRenderTargetView( mAOMapRT, clearColor );
	pd3dImmediateContext->RSSetViewports( 1, &mViewport );

	// Projection constants based on camera near and far clip planes.
	// projA = zf / (zf - zn)
	// projB = -zn * zf / (zf - zn)
	float projA = proj._33;
	float projB = proj._43;

	// Set variabled required by the SSAO shader.
	mSSAOFX->GetVariableByName("gRandomNormals")->AsShaderResource()->SetResource( mRandomNormalsSRV );
	mSSAOFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( depthMap );
	mSSAOFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( normalMap );
	mSSAOFX->GetVariableByName("gProjection")->AsMatrix()->SetMatrix( (float*)&proj );
	mSSAOFX->GetVariableByName("gInvProj")->AsMatrix()->SetMatrix( (float*)&XMMatrixInverse(&XMMatrixDeterminant(proj), proj) );
	mSSAOFX->GetVariableByName("gProjA")->AsScalar()->SetFloat(projA);
	mSSAOFX->GetVariableByName("gProjB")->AsScalar()->SetFloat(projB);
	mSSAOFX->GetVariableByName("gOffset")->AsScalar()->SetFloat( mOffset );
	mSSAOFX->GetVariableByName("gAOStart")->AsScalar()->SetFloat( mAOStart );
	mSSAOFX->GetVariableByName("gHemisphereRadius")->AsScalar()->SetFloat( mHemisphereRadius );

	// Render a full screen triangle to calculate ambient occlusion for every pixel.
	mSSAOFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	pd3dImmediateContext->Draw( 3, 0 );

	// Unbind depth map and normal map, because they will be used in other places.
	mSSAOFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mSSAOFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mSSAOFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	// Blur AO map n times. An intermediate render target is used to hold the
	// output of the first blur pass, and is then used as a shader input for
	// the second pass. This way we perform horizontal and vertical blur
	// separately.
	for (int i = 0; i < 1; ++i)
	{
		Blur( mAOMapSRV, mIntermediateBlurRT, true, depthMap, normalMap, pd3dImmediateContext );
		Blur( mIntermediateBlurSRV, mAOMapRT, false, depthMap, normalMap, pd3dImmediateContext );
	}
}

void SSAO::CompileShader( ID3D11Device *pd3dDevice, const char *filename, ID3DX11Effect **fx )
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

void SSAO::Blur( ID3D11ShaderResourceView *inputSRV, ID3D11RenderTargetView *outputRTV,
	bool horizontalBlur, ID3D11ShaderResourceView *depthMap, ID3D11ShaderResourceView *normalMap,
	ID3D11DeviceContext *pd3dImmediateContext )
{
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ID3D11RenderTargetView *rtv[1] = { outputRTV };
	pd3dImmediateContext->OMSetRenderTargets( 1, rtv, 0 );
	pd3dImmediateContext->ClearRenderTargetView( outputRTV, clearColor );

	mBilateralBlurFX->GetVariableByName("gTexelWidth")->AsScalar()->SetFloat( 1.0f / mViewport.Width );
	mBilateralBlurFX->GetVariableByName("gTexelHeight")->AsScalar()->SetFloat( 1.0f / mViewport.Height );
	mBilateralBlurFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( depthMap );
	mBilateralBlurFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( normalMap );
	mBilateralBlurFX->GetVariableByName("gImageToBlur")->AsShaderResource()->SetResource( inputSRV );

	ID3DX11EffectTechnique *tech;
	if (horizontalBlur)
		tech = mBilateralBlurFX->GetTechniqueByName("HorizontalBlur");
	else
		tech = mBilateralBlurFX->GetTechniqueByName("VerticalBlur");

	// Render a full screen triangle to apply blur on every pixel.
	tech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	pd3dImmediateContext->Draw( 3, 0 );

	mBilateralBlurFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mBilateralBlurFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mBilateralBlurFX->GetVariableByName("gImageToBlur")->AsShaderResource()->SetResource( 0 );
	tech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
}