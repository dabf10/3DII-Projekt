/*
	==================================================
	Fler intressanta saker att prova på
	==================================================
	Man borde kunna instansiera ljus eftersom det rendera som geometri (precis som
	när man instansierar annan geometri). Kanske även fungerar med quads.
*/

#include "App.h"
#include <WindowsX.h>

App::App() :
	mInputLayout( 0 ),
	mTxtHelper( 0 ),
	mFloorVB( 0 ),
	mFloorTex( 0 ),
	mLightRT( 0 ),
	mLightSRV( 0 ),
	mMainDepthDSV( 0 ),
	mMainDepthDSVReadOnly( 0 ),
	mMainDepthSRV( 0 ),
	mCompositeRT( 0 ),
	mCompositeSRV( 0 ),
	mFullscreenTextureFX( 0 ),
	mFillGBufferFX( 0 ),
	mDirectionalLightFX( 0 ),
	mDirectionalLightTech( 0 ),
	mPointLightFX( 0 ),
	mPointLightTech( 0 ),
	mSpotlightFX( 0 ),
	mSpotlightTech( 0 ),
	mCapsuleLightFX( 0 ),
	mCapsuleLightTech( 0 ),
	mProjPointLightFX( 0 ),
	mProjPointLightTech( 0 ),
	mProjPointLightColor( 0 ),
	mProjSpotlightFX( 0 ),
	mProjSpotlightTech( 0 ),
	mProjSpotlightColor( 0 ),
	mCombineLightFX( 0 ),
	mOldFilmFX( 0 ),
	mNoDepthTest( 0 ),
	mDepthGreaterEqual( 0 ),
	mAdditiveBlend( 0 ),
	mCullBack( 0 ),
	mCullFront( 0 ),
	mCullNone( 0 ),
	mSSAO( 0 ),
	mGBuffer( 0 )
{
}

App::~App()
{
}

//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool App::IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed )
{
    return true;
}

//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT App::OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc )
{
	mBackBufferSurfaceDesc = pBackBufferSurfaceDesc;

	HRESULT hr;
	V_RETURN( mDialogResourceManager.OnD3D11CreateDevice( pd3dDevice, DXUTGetD3D11DeviceContext() ) );
	V_RETURN( mD3DSettingsDlg.OnD3D11CreateDevice( pd3dDevice ) );
	mTxtHelper = new CDXUTTextHelper( pd3dDevice, DXUTGetD3D11DeviceContext(), &mDialogResourceManager, 15 );

	mModel = new Model();
	if ( !mModel->LoadOBJ( "bth.obj", false, pd3dDevice, mBthMaterialToUseForGroup, mBthMaterials ) )
		return E_FAIL;

	// Loop through every material (group) and load it's diffuse texture.
	for (UINT i = 0; i < mBthMaterials.size(); ++i)
	{
		ID3D11ShaderResourceView *srv;
		D3DX11CreateShaderResourceViewFromFileA(pd3dDevice, mBthMaterials[i].DiffuseTexture.c_str(),
			0, 0, &srv, 0);
		mMeshSRV.push_back(srv);
	}

	mSphereModel = new Model();
	if (!mSphereModel->LoadOBJ( "sphere.obj", true, pd3dDevice, mSphereMaterialToUseForGroup, mSphereMaterials ) )
		return E_FAIL;

	// Loop through every material (group) and load it's diffuse texture.
	for (UINT i = 0; i < mSphereMaterials.size(); ++i)
	{
		ID3D11ShaderResourceView *srv;
		D3DX11CreateShaderResourceViewFromFileA(pd3dDevice, mSphereMaterials[i].DiffuseTexture.c_str(),
			0, 0, &srv, 0);
		mSphereSRV.push_back(srv);
	}

	mConeModel = new Model();
	if (!mConeModel->LoadOBJ( "cone.obj", true, pd3dDevice, mConeMaterialToUseForGroup, mConeMaterials ) )
		return E_FAIL;

	if (FAILED(D3DX11CreateShaderResourceViewFromFileA( pd3dDevice, "ColorCube.dds", 0, 0, &mProjPointLightColor, 0 ) ) )
		return E_FAIL;
	if (FAILED(D3DX11CreateShaderResourceViewFromFileA( pd3dDevice, "ProjSpotColor.png", 0, 0, &mProjSpotlightColor, 0 ) ) )
		return E_FAIL;

	if (!BuildFX(pd3dDevice)) return E_FAIL;
	if (!BuildVertexLayout(pd3dDevice)) return E_FAIL;

	//
	// Floor geometry
	//

	D3D11_BUFFER_DESC vbDesc;
	vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbDesc.ByteWidth = 6 * 32; // 6 vertices á 32 byte (Pos: 12, Normal: 12, Tex: 8)
	vbDesc.CPUAccessFlags = 0;
	vbDesc.MiscFlags = 0;
	vbDesc.StructureByteStride = 32;
	vbDesc.Usage = D3D11_USAGE_IMMUTABLE;

	OBJLoader::Vertex verts[] = 
	{
		{ XMFLOAT3(-30, 0, 30), XMFLOAT2(0, 0), XMFLOAT3(0, 1, 0) },
		{ XMFLOAT3(30, 0, 30), XMFLOAT2(1, 0), XMFLOAT3(0, 1, 0) },
		{ XMFLOAT3(30, 0, -30), XMFLOAT2(1, 1), XMFLOAT3(0, 1, 0) },

		{ XMFLOAT3(-30, 0, 30), XMFLOAT2(0, 0), XMFLOAT3(0, 1, 0) },
		{ XMFLOAT3(30, 0, -30), XMFLOAT2(1, 1), XMFLOAT3(0, 1, 0) },
		{ XMFLOAT3(-30, 0, -30), XMFLOAT2(0, 1), XMFLOAT3(0, 1, 0) },
	};

	D3D11_SUBRESOURCE_DATA vinit;
	vinit.pSysMem = verts;
	V_RETURN( pd3dDevice->CreateBuffer( &vbDesc, &vinit, &mFloorVB ) );
	// Load floor texture
	V_RETURN( D3DX11CreateShaderResourceViewFromFileA( pd3dDevice, "floor.jpg", 0, 0, &mFloorTex, 0 ) );

	// No depth test
	D3D11_DEPTH_STENCIL_DESC dsDesc = D3D11_DEPTH_STENCIL_DESC( );
	dsDesc.DepthEnable = false;
	V_RETURN( pd3dDevice->CreateDepthStencilState( &dsDesc, &mNoDepthTest ) );
	// Depth test GEQUAL
	dsDesc.DepthEnable = true;
	dsDesc.DepthFunc = D3D11_COMPARISON_GREATER_EQUAL;
	V_RETURN( pd3dDevice->CreateDepthStencilState( &dsDesc, &mDepthGreaterEqual ) );

	// Additive blend state
	D3D11_BLEND_DESC blendDesc;
	ZeroMemory( &blendDesc, sizeof(blendDesc) );
	D3D11_RENDER_TARGET_BLEND_DESC rtbd;
	ZeroMemory( &rtbd, sizeof(rtbd) );
	rtbd.BlendEnable = true;
	rtbd.SrcBlend = D3D11_BLEND_ONE;
	rtbd.DestBlend = D3D11_BLEND_ONE;
	rtbd.BlendOp = D3D11_BLEND_OP_ADD;
	rtbd.SrcBlendAlpha = D3D11_BLEND_ONE;
	rtbd.DestBlendAlpha = D3D11_BLEND_ONE;
	rtbd.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rtbd.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	blendDesc.AlphaToCoverageEnable = false;
	blendDesc.RenderTarget[0] = rtbd;
	V_RETURN( pd3dDevice->CreateBlendState( &blendDesc, &mAdditiveBlend ) );

	// Cull back, cull front, and cull none RS states
	D3D11_RASTERIZER_DESC rsDesc = D3D11_RASTERIZER_DESC( );
	rsDesc.FillMode = D3D11_FILL_SOLID;
	rsDesc.CullMode = D3D11_CULL_BACK;
	V_RETURN( pd3dDevice->CreateRasterizerState( &rsDesc, &mCullBack ) );
	rsDesc.CullMode = D3D11_CULL_FRONT;
	V_RETURN( pd3dDevice->CreateRasterizerState( &rsDesc, &mCullFront ) );
	rsDesc.CullMode = D3D11_CULL_NONE;
	V_RETURN( pd3dDevice->CreateRasterizerState( &rsDesc, &mCullNone ) );
    
	return S_OK;
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void App::OnD3D11DestroyDevice( )
{
	mDialogResourceManager.OnD3D11DestroyDevice();
	mD3DSettingsDlg.OnD3D11DestroyDevice();
	DXUTGetGlobalResourceCache().OnDestroyDevice();
	SAFE_DELETE( mTxtHelper );

	SAFE_DELETE(mModel);
	SAFE_DELETE(mSphereModel);
	SAFE_DELETE(mConeModel);

	for (UINT i = 0; i < mMeshSRV.size(); ++i)
		SAFE_RELEASE(mMeshSRV[i]);
	mMeshSRV.clear();
	mBthMaterials.clear();
	mBthMaterialToUseForGroup.clear();

	for (UINT i = 0; i < mSphereSRV.size(); ++i)
		SAFE_RELEASE(mSphereSRV[i]);
	mSphereSRV.clear();
	mSphereMaterials.clear();
	mSphereMaterialToUseForGroup.clear();

	SAFE_RELEASE( mProjPointLightColor );
	SAFE_RELEASE( mProjSpotlightColor );
	
	SAFE_RELEASE( mInputLayout );

	SAFE_RELEASE( mFloorVB );
	SAFE_RELEASE( mFloorTex );

	SAFE_RELEASE( mFullscreenTextureFX );

	SAFE_RELEASE( mFillGBufferFX );

	SAFE_RELEASE( mDirectionalLightFX );
	SAFE_RELEASE( mPointLightFX );
	SAFE_RELEASE( mSpotlightFX );
	SAFE_RELEASE( mCapsuleLightFX );
	SAFE_RELEASE( mProjPointLightFX );
	SAFE_RELEASE( mProjSpotlightFX );

	SAFE_RELEASE( mCombineLightFX );
	SAFE_RELEASE( mOldFilmFX );

	SAFE_RELEASE( mNoDepthTest );
	SAFE_RELEASE( mDepthGreaterEqual );
	SAFE_RELEASE( mAdditiveBlend );
	SAFE_RELEASE( mCullBack );
	SAFE_RELEASE( mCullFront );
	SAFE_RELEASE( mCullNone );
}

//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool App::ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings )
{
	// I'll manage this myself, thank you very much.
	pDeviceSettings->d3d11.AutoCreateDepthStencil = false;

    return true;
}

//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT App::OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                          const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc )
{
	mBackBufferSurfaceDesc = pBackBufferSurfaceDesc;

	HRESULT hr;
	V_RETURN( mDialogResourceManager.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );
	V_RETURN( mD3DSettingsDlg.OnD3D11ResizedSwapChain( pd3dDevice, pBackBufferSurfaceDesc ) );

	mHUD.SetLocation( pBackBufferSurfaceDesc->Width - 170, 0 );
	mHUD.SetSize( 170, 350 );
	
	// Calculate aspect ratio based on new width and height, and use it to update
	// camera perspective matrix.
	float aspectRatio = static_cast<float>(pBackBufferSurfaceDesc->Width) / pBackBufferSurfaceDesc->Height;
	mCamera.SetLens(0.25f * D3DX_PI, aspectRatio, 0.1f, 1000.0f);

	V_RETURN( CreateGBuffer( pd3dDevice, pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height ) );

	mSSAO = new SSAO( pd3dDevice, pBackBufferSurfaceDesc->Width / 2, pBackBufferSurfaceDesc->Height / 2, 18, 0.1f, 1.5f );
	mGBuffer = new GBuffer( pd3dDevice, pBackBufferSurfaceDesc->Width, pBackBufferSurfaceDesc->Height );

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void App::OnD3D11ReleasingSwapChain( )
{
	mDialogResourceManager.OnD3D11ReleasingSwapChain();
	
	SAFE_RELEASE( mLightRT );
	SAFE_RELEASE( mLightSRV );
	SAFE_RELEASE( mMainDepthDSV );
	SAFE_RELEASE( mMainDepthDSVReadOnly );
	SAFE_RELEASE( mMainDepthSRV );
	SAFE_RELEASE( mCompositeRT );
	SAFE_RELEASE( mCompositeSRV );
	SAFE_DELETE( mSSAO );
	SAFE_DELETE( mGBuffer );
}


//--------------------------------------------------------------------------------------
// Call if device was removed.  Return true to find a new device, false to quit
//--------------------------------------------------------------------------------------
bool App::OnDeviceRemoved( )
{
    return true;
}

//--------------------------------------------------------------------------------------
// Initialize application.
//--------------------------------------------------------------------------------------
bool App::Init( )
{
	//
	// Initialize GUI controls such as buttons and sliders.
	//
	
	mD3DSettingsDlg.Init( &mDialogResourceManager );
	mHUD.Init( &mDialogResourceManager );
	D3DCOLOR dlgColor = 0x88888888; // Semi-transparent background for the dialog
	mHUD.SetBackgroundColors( dlgColor );

	mHUD.SetCallback( &App::OnGUIEvent, this );
	int iY = 10;
	mHUD.AddButton( IDC_TOGGLEFULLSCREEN, L"Toggle full screen", 35, iY, 125, 22 );
	mHUD.AddButton( IDC_CHANGEDEVICE, L"Change device (F2)", 35, iY += 24, 125, 22, VK_F2 );

	// Options UI


	//
	// Initialize rest of application.
	//

	mLastMousePos.x = 0;
	mLastMousePos.y = 0;

	mCamera.LookAt(XMFLOAT3( 20.0f, 20.0f, -30.0f ), XMFLOAT3( 0, 0, 0 ), XMFLOAT3( 0, 1, 0 ) );
	mCamera.UpdateViewMatrix();
	
	float uniformScaleFactor = 0.2f;
	XMMATRIX scale = XMMatrixScaling(uniformScaleFactor, uniformScaleFactor, uniformScaleFactor);
	XMMATRIX rotation = XMMatrixRotationY(XMConvertToRadians(0));
	XMMATRIX translation = XMMatrixTranslation(0.0f, 10.0f, -10.0f);
	XMMATRIX world = scale * rotation * translation;
	XMStoreFloat4x4(&mBthWorld[0], world);
	translation = XMMatrixTranslation( 0.0f, 10.0f, 10.0f );
	world = scale * rotation * translation;
	XMStoreFloat4x4(&mBthWorld[1], world);

	uniformScaleFactor = 5;
	scale = XMMatrixScaling(uniformScaleFactor, uniformScaleFactor, uniformScaleFactor);
	translation = XMMatrixTranslation(15, 10, 0);
	world = scale * rotation * translation;
	XMStoreFloat4x4(&mSphereWorld, world);

	XMVECTOR dir = XMVectorSet(0.0000000000000000000001f, 1, 0, 0);
	XMVECTOR up = XMVectorSet(0, 1, 0, 0);
	rotation = XMMatrixTranspose(XMMatrixLookAtLH(XMVectorSet(0, 0, 0, 1), dir, up));
	float angle = 18.0f;
	float radius = 20.0f;
	float xyScale = tanf(XMConvertToRadians(angle)) * radius;
	scale = XMMatrixScaling(xyScale, xyScale, radius);
	translation = XMMatrixTranslation(-15, 0, 0);
	world = scale * rotation * translation;
	XMStoreFloat4x4(&mConeWorld, world);

	XMStoreFloat4x4( &mFloorWorld, XMMatrixIdentity() );

	uniformScaleFactor = 1.0f;
	scale = XMMatrixScaling(uniformScaleFactor, uniformScaleFactor, uniformScaleFactor);
	rotation = XMMatrixRotationY(XMConvertToRadians(0));
	translation = XMMatrixTranslation(0, 0, 0);
	world = scale * rotation * translation;

	return true;
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is used
//--------------------------------------------------------------------------------------
void App::OnFrameMove( double fTime, float fElapsedTime )
{
	float cameraSpeed = 20.0f;

	// Control the camera.
	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(cameraSpeed * fElapsedTime);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-cameraSpeed * fElapsedTime);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-cameraSpeed * fElapsedTime);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(cameraSpeed * fElapsedTime);

	mCamera.UpdateViewMatrix();
}


//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void App::OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext,
                                  double fTime, float fElapsedTime )
{
	// Clear render target and the depth stencil.
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	
	ID3D11RenderTargetView *rtv = DXUTGetD3D11RenderTargetView();
	pd3dImmediateContext->ClearRenderTargetView(rtv, clearColor);
	pd3dImmediateContext->ClearDepthStencilView(mMainDepthDSV, D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL, 1.0f, 0);
	
	// If the settings dialog is being shown, then render it instead of rendering
	// the app's scene.
	if (mD3DSettingsDlg.IsActive() )
	{
		mD3DSettingsDlg.OnRender( fElapsedTime );
		return;
	}

	pd3dImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D11RenderTargetView *rtvs[] = { mGBuffer->ColorRT(), mGBuffer->NormalRT() };
	// Clearing the GBuffer does not require any depth test.
	pd3dImmediateContext->OMSetRenderTargets( 2, rtvs, 0 );

	mGBuffer->Clear( pd3dImmediateContext );
	
	// Enable depth testing when rendering scene.
	pd3dImmediateContext->OMSetRenderTargets( 2, rtvs, mMainDepthDSV );

	//
	// Draw the scene.
	//
	{
		pd3dImmediateContext->IASetInputLayout( mInputLayout );

		//
		// Loop through two BTH logos
		//
		for (int logo = 0; logo < 2; ++logo)
		{
			XMMATRIX world = XMLoadFloat4x4(&mBthWorld[logo]);
			XMMATRIX worldView = world * mCamera.View();
			XMMATRIX wvp = world * mCamera.ViewProj();
			XMMATRIX worldViewInvTrp = XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(worldView), worldView));

			// Set object specific constants.
			mFillGBufferFX->GetVariableByName("gWorldViewInvTrp")->AsMatrix()->SetMatrix((float*)&worldViewInvTrp);
			mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);

			// Loop through every submesh the current mesh consists of.
			for (UINT s = 0; s < mModel->SubMeshes(); ++s)
			{
				mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mMeshSRV[mBthMaterialToUseForGroup[s]]);

				mFillGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
				mModel->RenderSubMesh( pd3dImmediateContext, s );
			}
		}

		//
		// Floor
		//

		XMMATRIX world = XMLoadFloat4x4( &mFloorWorld );
		XMMATRIX worldView = world * mCamera.View();
		XMMATRIX wvp = world * mCamera.ViewProj();
		XMMATRIX worldViewInvTrp = XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(worldView), worldView));

		// Set per object constants.
		mFillGBufferFX->GetVariableByName("gWorldViewInvTrp")->AsMatrix()->SetMatrix((float*)&worldViewInvTrp);
		mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);
		mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mFloorTex);

		UINT strides = 32;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers( 0, 1, &mFloorVB, &strides, &offsets );

		mFillGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
		pd3dImmediateContext->Draw( 6, 0 );

		mFillGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		//
		// Sphere model
		//
		
		world = XMLoadFloat4x4(&mSphereWorld);
		worldView = world * mCamera.View();
		wvp = world * mCamera.ViewProj();
		worldViewInvTrp = XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(worldView), worldView));

		// Set object specific constants.
		mFillGBufferFX->GetVariableByName("gWorldViewInvTrp")->AsMatrix()->SetMatrix((float*)&worldViewInvTrp);
		mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);

		// Loop through every submesh the current mesh consists of.
		for (UINT s = 0; s < mSphereModel->SubMeshes(); ++s)
		{
			mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mSphereSRV[mSphereMaterialToUseForGroup[s]]);

			mFillGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
			mSphereModel->RenderSubMesh( pd3dImmediateContext, s );
		}

		//
		// Cone model
		//

		world = XMLoadFloat4x4(&mConeWorld);
		worldView = world * mCamera.View();
		wvp = world * mCamera.ViewProj();
		worldViewInvTrp = XMMatrixTranspose(XMMatrixInverse(&XMMatrixDeterminant(worldView), worldView));

		// Set object specific constants.
		mFillGBufferFX->GetVariableByName("gWorldViewInvTrp")->AsMatrix()->SetMatrix((float*)&worldViewInvTrp);
		mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);
		mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mMeshSRV[0]);

		mFillGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
		mConeModel->Render( pd3dImmediateContext );
	}

	//
	// Render lights
	//

	RenderLights( pd3dImmediateContext, fTime );

	//
	// SSAO
	//

	// Because SSAO changes viewport (normally only renders to a quarter size of backbuffer)
	// we need to reset the viewport after.
	D3D11_VIEWPORT fullViewport;
	UINT numViewports = 1;
	pd3dImmediateContext->RSGetViewports(&numViewports, &fullViewport);

	mSSAO->CalculateSSAO(
		mMainDepthSRV, mGBuffer->NormalSRV(), mCamera.Proj(), pd3dImmediateContext );

	pd3dImmediateContext->RSSetViewports(1, &fullViewport);
	pd3dImmediateContext->OMSetRenderTargets( 1, &mCompositeRT, 0 );

	//
	// Render a full-screen quad that combines the light from the light map
	// with the color map from the G-Buffer
	//
	{		
		mCombineLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
		mCombineLightFX->GetVariableByName("gLightMap")->AsShaderResource()->SetResource(mLightSRV);

		mCombineLightFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
		pd3dImmediateContext->Draw( 3, 0 );

		// Unbind shader resources
		mCombineLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
		mCombineLightFX->GetVariableByName("gLightMap")->AsShaderResource()->SetResource( 0 );
		mCombineLightFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	}

	// Render to back buffer.
	pd3dImmediateContext->OMSetRenderTargets( 1, &rtv, 0 );

	//
	// Generate an old-film looking effect
	//
	//{
	//	float sepia = 0.6f;
	//	float noise = 0.06f;
	//	float scratch = 0.3f;
	//	float innerVignetting = 1.0f - 0.3f;
	//	float outerVignetting = 1.4f - 0.3f;
	//	float random = (float)rand()/(float)RAND_MAX;
	//	float timeLapse = fElapsedTime;

	//	mOldFilmFX->GetVariableByName("gCompositeImage")->AsShaderResource()->SetResource(mCompositeSRV);
	//	mOldFilmFX->GetVariableByName("gSepiaValue")->AsScalar()->SetFloat(sepia);
	//	mOldFilmFX->GetVariableByName("gNoiseValue")->AsScalar()->SetFloat(noise);
	//	mOldFilmFX->GetVariableByName("gScratchValue")->AsScalar()->SetFloat(scratch);
	//	mOldFilmFX->GetVariableByName("gInnerVignetting")->AsScalar()->SetFloat(innerVignetting);
	//	mOldFilmFX->GetVariableByName("gOuterVignetting")->AsScalar()->SetFloat(outerVignetting);
	//	mOldFilmFX->GetVariableByName("gRandomValue")->AsScalar()->SetFloat(random);
	//	mOldFilmFX->GetVariableByName("gTimeLapse")->AsScalar()->SetFloat(timeLapse);

	//	mOldFilmFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	//	pd3dImmediateContext->Draw( 3, 0 );

	//	// Unbind shader resources
	//	mOldFilmFX->GetVariableByName("gCompositeImage")->AsShaderResource()->SetResource( 0 );
	//	mOldFilmFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	//}

	// Render a full screen triangle to place content of CompositeSRV on the back buffer.
	{
		mFullscreenTextureFX->GetVariableByName("gTexture")->AsShaderResource()->SetResource(mCompositeSRV);

		mFullscreenTextureFX->GetTechniqueByName("MultiChannel")->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
		pd3dImmediateContext->Draw( 3, 0 );

		mFullscreenTextureFX->GetVariableByName("gTexture")->AsShaderResource()->SetResource( 0 );
		mFullscreenTextureFX->GetTechniqueByName("MultiChannel")->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	}

	//
	// Full-screen textured quad
	//

	// Specify 1 or 4.
	UINT numImages = 0;

	if (GetAsyncKeyState(VK_NUMPAD1) & 0x8000)
		numImages = 1;
	else if (GetAsyncKeyState(VK_NUMPAD4) & 0x8000)
		numImages = 4;

	if (numImages)
	{
		// The first code is just to easily display 1 full screen image or
		// 4 smaller in quadrants. Simply select what resource views to use
		// and how many of those to draw.
		ID3D11ShaderResourceView *srvs[4] = {
			mSSAO->AOMap(),
			mGBuffer->NormalSRV(),
			mLightSRV,
			mGBuffer->ColorSRV()
		};

		D3D11_VIEWPORT vp[4];
		for (int i = 0; i < 4; ++i)
		{
			vp[i].MinDepth = 0.0f;
			vp[i].MaxDepth = 1.0f;
			vp[i].Width = mBackBufferSurfaceDesc->Width / 2;
			vp[i].Height = mBackBufferSurfaceDesc->Height / 2;
			vp[i].TopLeftX = (i % 2) * mBackBufferSurfaceDesc->Width / 2;
			vp[i].TopLeftY = (UINT)(0.5f * i) * mBackBufferSurfaceDesc->Height / 2;
		}

		if (numImages == 1)
		{
			vp[0].Width = mBackBufferSurfaceDesc->Width;
			vp[0].Height = mBackBufferSurfaceDesc->Height;
		}

		// Here begins actual render code

		for (int i = 0; i < numImages; ++i)
		{
			ID3DX11EffectTechnique *tech = 0;
			if (srvs[i] == mSSAO->AOMap() || srvs[i] == mMainDepthSRV)
				tech = mFullscreenTextureFX->GetTechniqueByName("SingleChannel");
			else
				tech = mFullscreenTextureFX->GetTechniqueByName("MultiChannel");
			
			D3DX11_TECHNIQUE_DESC techDesc;
			tech->GetDesc(&techDesc);

			pd3dImmediateContext->RSSetViewports(1, &vp[i]);
			mFullscreenTextureFX->GetVariableByName("gTexture")->AsShaderResource()->SetResource(srvs[i]);

			tech->GetPassByIndex(0)->Apply( 0, pd3dImmediateContext );
			pd3dImmediateContext->Draw( 3, 0 );

			mFullscreenTextureFX->GetVariableByName("gTexture")->AsShaderResource()->SetResource( 0 );
			tech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
		}

		pd3dImmediateContext->RSSetViewports(1, &fullViewport);
	}

	//
	// Render the UI
	//

	mHUD.OnRender( fElapsedTime );
	RenderText();
}


//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT App::MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                          bool* pbNoFurtherProcessing )
{
	// Always allow dialog resource manager calls to handle global messages
	// so GUI state is updated correctly.
	*pbNoFurtherProcessing = mDialogResourceManager.MsgProc( hWnd, uMsg, wParam, lParam );
	if (*pbNoFurtherProcessing )
		return 0;

	// Pass messages to settings dialog if it's active.
	if (mD3DSettingsDlg.IsActive() )
	{
		mD3DSettingsDlg.MsgProc( hWnd, uMsg, wParam, lParam );
		return 0;
	}

	// Give the dialogs a chance to handle the message first.
	*pbNoFurtherProcessing = mHUD.MsgProc( hWnd, uMsg, wParam, lParam );
	if (*pbNoFurtherProcessing )
		return 0;
		
	switch ( uMsg )
	{
	case WM_MOUSEMOVE:
		OnMouseMove( wParam, GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ) );
		break;
	}

    return 0;
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void App::OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown )
{
}


//--------------------------------------------------------------------------------------
// Handle mouse button presses
//--------------------------------------------------------------------------------------
void App::OnMouse( bool bLeftButtonDown, bool bRightButtonDown, bool bMiddleButtonDown,
                       bool bSideButton1Down, bool bSideButton2Down, int nMouseWheelDelta,
                       int xPos, int yPos )
{
}

void App::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

bool App::BuildFX(ID3D11Device *device)
{
	//
	// Full-screen quad
	//

	if (!CompileShader( device, "Shaders/FullscreenTexture.fx", &mFullscreenTextureFX ))
		return false;

	//
	// GBuffer
	//

	if (!CompileShader( device, "Shaders/GBuffer.fx", &mFillGBufferFX ))
		return false;

	//
	// DirectionalLight
	//

	if (!CompileShader( device, "Shaders/DirectionalLight.fx", &mDirectionalLightFX ))
		return false;

	mDirectionalLightTech = mDirectionalLightFX->GetTechniqueByIndex(0);

	//
	// CombineLight
	//

	if (!CompileShader( device, "Shaders/CombineLight.fx", &mCombineLightFX ))
		return false;

	//
	// PointLight
	//

	if (!CompileShader( device, "Shaders/PointLight.fx", &mPointLightFX ))
		return false;

	mPointLightTech = mPointLightFX->GetTechniqueByIndex(0);

	//
	// Spotlight
	//

	if (!CompileShader( device, "Shaders/Spotlight.fx", &mSpotlightFX ))
		return false;

	mSpotlightTech = mSpotlightFX->GetTechniqueByIndex(0);

	//
	// CapsuleLight
	//

	if (!CompileShader( device, "Shaders/CapsuleLight.fx", &mCapsuleLightFX ))
		return false;

	mCapsuleLightTech = mCapsuleLightFX->GetTechniqueByIndex(0);

	//
	// ProjPointLight
	//

	if (!CompileShader( device, "Shaders/ProjPointLight.fx", &mProjPointLightFX ))
		return false;

	mProjPointLightTech = mProjPointLightFX->GetTechniqueByIndex(0);

	//
	// ProjSpotlight
	//

	if (!CompileShader( device, "Shaders/ProjSpotlight.fx", &mProjSpotlightFX ))
		return false;

	mProjSpotlightTech = mProjSpotlightFX->GetTechniqueByIndex(0);

	//
	// OldFilm
	//

	if (!CompileShader( device, "Shaders/OldFilm.fx", &mOldFilmFX ))
		return false;

	return true;
}

bool App::CompileShader( ID3D11Device *device, const char *filename, ID3DX11Effect **fx )
{
	DWORD shaderFlags = D3D10_SHADER_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
	shaderFlags |= D3D10_SHADER_DEBUG;
#endif

	ID3D10Blob *compiledShader;
	ID3D10Blob *errorMsgs;
	HRESULT hr;

	hr = D3DX11CompileFromFileA( filename, 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0 );

	// errorMsgs can store errors or warnings.
	if (errorMsgs)
	{
		MessageBoxA(0, (char*)errorMsgs->GetBufferPointer(), "D3DX11CompileFromFile", MB_OK);
		SAFE_RELEASE(errorMsgs);
		return false;
	}

	// Even if there are no error messages, check to make sure there were no other errors.
	if (FAILED(hr))
	{
		DXTraceA(__FILE__, __LINE__, hr, "D3DX11CompileFromFile", true);
		return false;
	}

	if (FAILED(D3DX11CreateEffectFromMemory(compiledShader->GetBufferPointer(),
		compiledShader->GetBufferSize(), 0, device, fx))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	return true;
}

bool App::BuildVertexLayout(ID3D11Device *device)
{
	// Create the vertex input layout.
	D3D11_INPUT_ELEMENT_DESC vertexDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};

	// Create the input layout.
	D3DX11_PASS_DESC passDesc;
	mFillGBufferFX->GetTechniqueByIndex( 0 )->GetPassByIndex( 0 )->GetDesc( &passDesc );
	if (FAILED(device->CreateInputLayout(vertexDesc, 3, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &mInputLayout))) return false;

	return true;
}

void App::OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl *pControl )
{
	switch (nControlID )
	{
	case IDC_TOGGLEFULLSCREEN: DXUTToggleFullScreen(); break;
	case IDC_CHANGEDEVICE: mD3DSettingsDlg.SetActive( !mD3DSettingsDlg.IsActive() ); break;
	}
}

void App::RenderText()
{
	mTxtHelper->Begin();
	mTxtHelper->SetInsertionPos( 5, 5 );
	mTxtHelper->SetForegroundColor( D3DXCOLOR( 1.0f, 1.0f, 0.0f, 1.0f ) );
	mTxtHelper->DrawTextLine( DXUTGetFrameStats( DXUTIsVsyncEnabled() ) );
	mTxtHelper->DrawTextLine( DXUTGetDeviceStats() );

	mTxtHelper->End();
}

HRESULT App::CreateGBuffer( ID3D11Device *device, UINT width, UINT height )
{
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

	//
	// Light (not actually G-Buffer, but used to accumulate light)
	//

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex) );
	V_RETURN( device->CreateRenderTargetView( tex, NULL, &mLightRT ) );
	V_RETURN( device->CreateShaderResourceView( tex, NULL, &mLightSRV ) );
	
	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// Composite
	//

	// TODO: Is it possible to render stuff to the regular render target,
	// and use that as input for post-processing so that we don't need this
	// intermediate one? It seems strange though, to use a resource as both
	// render target and shader input at the same time.
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex ) );
	V_RETURN( device->CreateRenderTargetView( tex, NULL, &mCompositeRT ) );
	V_RETURN( device->CreateShaderResourceView( tex, NULL, &mCompositeSRV ) );

	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// Depth/stencil buffer and view
	//

	texDesc.Width = mBackBufferSurfaceDesc->Width;
	texDesc.Height = mBackBufferSurfaceDesc->Height;
	texDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
	texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex ) );

	D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
	dsvDesc.Flags = 0;
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Texture2D.MipSlice = 0;
	V_RETURN( device->CreateDepthStencilView( tex, &dsvDesc, &mMainDepthDSV ) );

	dsvDesc.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
	V_RETURN( device->CreateDepthStencilView( tex, &dsvDesc, &mMainDepthDSVReadOnly ) );

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mMainDepthSRV ) );

	// Views saves reference
	SAFE_RELEASE( tex );


	return S_OK;
}

void App::RenderDirectionalLight( ID3D11DeviceContext *pd3dImmediateContext, XMFLOAT3 color, XMFLOAT3 direction )
{
	mDirectionalLightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&color);
	mDirectionalLightFX->GetVariableByName("gLightDirectionVS")->AsVector()->SetFloatVector((float*)&XMVector4Transform(XMLoadFloat3(&direction), mCamera.View()));

	mDirectionalLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	pd3dImmediateContext->Draw( 3, 0 );
}

void App::RenderPointLight( ID3D11DeviceContext *pd3dImmediateContext, XMFLOAT3 color,
		XMFLOAT3 position, float radius, float intensity )
{
	// Compute the light world matrix. Scale according to light radius,
	// and translate it to light position.
	XMMATRIX sphereWorld = XMMatrixScaling(radius, radius, radius) *
		XMMatrixTranslation(position.x, position.y, position.z);
	
	mPointLightFX->GetVariableByName("gWorldView")->AsMatrix()->SetMatrix((float*)&XMMatrixMultiply(sphereWorld, mCamera.View()));
	mPointLightFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&XMMatrixMultiply(sphereWorld, mCamera.ViewProj()));
	mPointLightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&color);
	mPointLightFX->GetVariableByName("gLightPositionVS")->AsVector()->SetFloatVector((float*)&XMVector3Transform(XMLoadFloat3(&position), mCamera.View()));
	mPointLightFX->GetVariableByName("gLightRadius")->AsScalar()->SetFloat(radius);
	mPointLightFX->GetVariableByName("gLightIntensity")->AsScalar()->SetFloat(intensity);
		
	//// Calculate the distance between the camera and light center.
	//XMVECTOR vCameraToCenter = mCamera.GetPositionXM() - XMLoadFloat3(&position);
	//XMVECTOR cameraToCenterSq = XMVector3Dot(vCameraToCenter, vCameraToCenter);

	//// If we are inside the light volume, draw the sphere's inside face
	//bool inside = XMVectorGetX(cameraToCenterSq) < radius * radius;
	//if (inside)
	//	pd3dImmediateContext->RSSetState( mCullFront );
	//else
	//	pd3dImmediateContext->RSSetState( mCullBack );

	// Render the light volume.
	mPointLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	mSphereModel->Render( pd3dImmediateContext );

	//// Reset culling mode back to normal, else other meshes might not render correctly.
	//if (inside)
	//	pd3dImmediateContext->RSSetState( mCullBack );
}

void App::RenderSpotlight( ID3D11DeviceContext *pd3dImmediateContext, XMFLOAT3 color,
		XMFLOAT3 position, XMFLOAT3 direction, float range, float outerAngleDeg,
		float innerAngleDeg )
{
	static XMVECTOR zero = XMVectorSet(0, 0, 0, 1);
	static XMVECTOR up = XMVectorSet(0, 1, 0, 0);

	// Construct the cone world matrix.
	// Add a small epsilon to x because if the light is aimed straight up, the
	// rotation matrix is gonna have a bad time.
	XMVECTOR directionXM = XMVector3Normalize(XMVectorSet(direction.x + 0.000000000000000000001f, direction.y, direction.z, 0.0f));
	float outerAngleRad = XMConvertToRadians(outerAngleDeg);
	float innerAngleRad = XMConvertToRadians(innerAngleDeg);
	float xyScale = tanf(outerAngleRad) * range;
	XMMATRIX coneWorld = XMMatrixScaling(xyScale, xyScale, range) *
		XMMatrixTranspose(XMMatrixLookAtLH(zero, directionXM, up)) *
		XMMatrixTranslation(position.x, position.y, position.z);

	mSpotlightFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&XMMatrixMultiply(coneWorld, mCamera.ViewProj()));
	mSpotlightFX->GetVariableByName("gWorldView")->AsMatrix()->SetMatrix((float*)&XMMatrixMultiply(coneWorld, mCamera.View()));
	mSpotlightFX->GetVariableByName("gProj")->AsMatrix()->SetMatrix((float*)&mCamera.Proj());
	
	mSpotlightFX->GetVariableByName("gDirectionVS")->AsVector()->SetFloatVector((float*)&XMVector4Transform(directionXM, mCamera.View()));
	mSpotlightFX->GetVariableByName("gCosOuter")->AsScalar()->SetFloat(cosf(outerAngleRad));
	mSpotlightFX->GetVariableByName("gCosInner")->AsScalar()->SetFloat(cosf(innerAngleRad));
	mSpotlightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&color);
	mSpotlightFX->GetVariableByName("gLightPositionVS")->AsVector()->SetFloatVector((float*)&XMVector3Transform(XMLoadFloat3(&position), mCamera.View()));
	mSpotlightFX->GetVariableByName("gLightRangeRcp")->AsScalar()->SetFloat(1.0f / range);

	//// Test to check if the camera is inside light volume.
	//bool inside = false;
	//XMVECTOR tipToPoint = mCamera.GetPositionXM() - XMLoadFloat3(&position);
	//// Project the vector from cone tip to test point onto light direction
	//// to find the point's distance along the axis.
	//XMVECTOR coneDist = XMVector3Dot(tipToPoint, directionXM);
	//// Reject values outside 0 <= coneDist <= coneHeight
	//if (0 <= XMVectorGetX(coneDist) && XMVectorGetX(coneDist) <= range)
	//{
	//	// Radius at point.
	//	float radiusAtPoint = (XMVectorGetX(coneDist) / range) * xyScale;
	//	// Calculate the point's orthogonal distance to axis and compare to
	//	// cone radius (radius at point).
	//	XMVECTOR pointOrthDirection = tipToPoint - XMVectorGetX(coneDist) * directionXM;
	//	XMVECTOR orthDistSq = XMVector3Dot(pointOrthDirection, pointOrthDirection);

	//	inside = (XMVectorGetX(orthDistSq) < radiusAtPoint * radiusAtPoint);
	//}
	
	//// If we are inside the light volume, draw the cone's inside face.
	//if (inside)
	//{
	//	pd3dImmediateContext->RSSetState( mCullFront );
	//}
	//else
	//	pd3dImmediateContext->RSSetState( mCullBack );

	mSpotlightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	mConeModel->Render( pd3dImmediateContext );

	//// Reset culling mode back to normal, else other meshes might not render correctly.
	//if (inside)
	//	pd3dImmediateContext->RSSetState( mCullBack );
}

void App::RenderCapsuleLight( ID3D11DeviceContext *pd3dImmediateContext,
	XMFLOAT3 color, XMFLOAT3 position, XMFLOAT3 direction, float range, float length )
{
	XMVECTOR directionXM = XMVector3Normalize(XMVectorSet(direction.x, direction.y, direction.z, 0.0f));

	mCapsuleLightFX->GetVariableByName("gProj")->AsMatrix()->SetMatrix((float*)&mCamera.Proj());
	
	mCapsuleLightFX->GetVariableByName("gLightDirectionVS")->AsVector()->SetFloatVector((float*)&XMVector4Transform(directionXM, mCamera.View()));
	mCapsuleLightFX->GetVariableByName("gLightPositionVS")->AsVector()->SetFloatVector((float*)&XMVector3Transform(XMLoadFloat3(&position), mCamera.View()));
	mCapsuleLightFX->GetVariableByName("gLightRangeRcp")->AsScalar()->SetFloat(1.0f / range);
	mCapsuleLightFX->GetVariableByName("gLightLength")->AsScalar()->SetFloat(length);
	mCapsuleLightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&color);

	mCapsuleLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	pd3dImmediateContext->Draw( 3, 0 );
}

void App::RenderProjPointLight( ID3D11DeviceContext *pd3dImmediateContext, ID3D11ShaderResourceView *tex,
		XMFLOAT3 position, float radius, float intensity, float fTime )
{
	// Compute the light world matrix. Scale according to light radius,
	// and translate it to light position.
	XMMATRIX sphereWorld = XMMatrixScaling(radius, radius, radius) *
		XMMatrixRotationZ(fTime) * XMMatrixRotationY(fTime) *
		XMMatrixTranslation(position.x, position.y, position.z);
	XMMATRIX worldView = XMMatrixMultiply( sphereWorld, mCamera.View() );
	
	mProjPointLightFX->GetVariableByName("gLightTransform")->AsMatrix()->SetMatrix((float*)&XMMatrixInverse(&XMMatrixDeterminant(worldView), worldView));
	mProjPointLightFX->GetVariableByName("gWorldView")->AsMatrix()->SetMatrix((float*)&worldView);
	mProjPointLightFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&XMMatrixMultiply(sphereWorld, mCamera.ViewProj()));
	mProjPointLightFX->GetVariableByName("gProjLightTex")->AsShaderResource()->SetResource(tex);
	mProjPointLightFX->GetVariableByName("gLightPositionVS")->AsVector()->SetFloatVector((float*)&XMVector3Transform(XMLoadFloat3(&position), mCamera.View()));
	mProjPointLightFX->GetVariableByName("gLightRadius")->AsScalar()->SetFloat(radius);
	mProjPointLightFX->GetVariableByName("gLightIntensity")->AsScalar()->SetFloat(intensity);
		
	//// Calculate the distance between the camera and light center.
	//XMVECTOR vCameraToCenter = mCamera.GetPositionXM() - XMLoadFloat3(&position);
	//XMVECTOR cameraToCenterSq = XMVector3Dot(vCameraToCenter, vCameraToCenter);

	//// If we are inside the light volume, draw the sphere's inside face
	//bool inside = XMVectorGetX(cameraToCenterSq) < radius * radius;
	//if (inside)
	//	pd3dImmediateContext->RSSetState( mCullFront );
	//else
	//	pd3dImmediateContext->RSSetState( mCullBack );

	// Render the light volume.
	mProjPointLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	mSphereModel->Render( pd3dImmediateContext );

	//// Reset culling mode back to normal, else other meshes might not render correctly.
	//if (inside)
	//	pd3dImmediateContext->RSSetState( mCullBack );

	mProjPointLightFX->GetVariableByName("gProjLightTex")->AsShaderResource()->SetResource(0);
	mProjPointLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
}

void App::RenderProjSpotlight( ID3D11DeviceContext *pd3dImmediateContext, ID3D11ShaderResourceView *tex,
		XMFLOAT3 position, XMFLOAT3 direction, float range, float outerAngleDeg,
		float innerAngleDeg )
{
	static XMVECTOR zero = XMVectorSet(0, 0, 0, 1);
	static XMVECTOR up = XMVectorSet(0, 1, 0, 0);

	// Construct the cone world matrix.
	// Add a small epsilon to x because if the light is aimed straight up, the
	// rotation matrix is gonna have a bad time.
	XMVECTOR directionXM = XMVector3Normalize(XMVectorSet(direction.x + 0.000000000000000000001f, direction.y, direction.z, 0.0f));
	float outerAngleRad = XMConvertToRadians(outerAngleDeg);
	float innerAngleRad = XMConvertToRadians(innerAngleDeg);
	float xyScale = tanf(outerAngleRad) * range;
	XMMATRIX coneWorld = XMMatrixScaling(xyScale, xyScale, range) *
		XMMatrixTranspose(XMMatrixLookAtLH(zero, directionXM, up)) *
		XMMatrixTranslation(position.x, position.y, position.z);
	XMMATRIX worldView = XMMatrixMultiply(coneWorld, mCamera.View());
	// 2 * halfAngle = fov / 2 ==> fov = 4 * halfAngle
	XMMATRIX lightViewProj = XMMatrixInverse(&XMMatrixDeterminant(worldView), worldView) * XMMatrixPerspectiveFovLH(outerAngleRad * 4, 1.0f, 0.0001f * range, range);
	//XMMatrixSet(tanf(XM_PIDIV2 - 2*outerAngleRad), 0, 0, 0,
	//			0, tanf(XM_PIDIV2 - 2*outerAngleRad), 0, 0,
	//			0, 0, range / (range - 0.0001f * range), 1,
	//			0, 0, -0.0001f * range * range / (range - 0.0001f * range), 0);
	
	mProjSpotlightFX->GetVariableByName("gLightViewProj")->AsMatrix()->SetMatrix((float*)&lightViewProj);
	mProjSpotlightFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&XMMatrixMultiply(coneWorld, mCamera.ViewProj()));
	mProjSpotlightFX->GetVariableByName("gWorldView")->AsMatrix()->SetMatrix((float*)&worldView);
	mProjSpotlightFX->GetVariableByName("gProj")->AsMatrix()->SetMatrix((float*)&mCamera.Proj());
	
	mProjSpotlightFX->GetVariableByName("gDirectionVS")->AsVector()->SetFloatVector((float*)&XMVector4Transform(directionXM, mCamera.View()));
	mProjSpotlightFX->GetVariableByName("gCosOuter")->AsScalar()->SetFloat(cosf(outerAngleRad));
	mProjSpotlightFX->GetVariableByName("gCosInner")->AsScalar()->SetFloat(cosf(innerAngleRad));
	mProjSpotlightFX->GetVariableByName("gProjLightTex")->AsShaderResource()->SetResource(tex);
	mProjSpotlightFX->GetVariableByName("gLightPositionVS")->AsVector()->SetFloatVector((float*)&XMVector3Transform(XMLoadFloat3(&position), mCamera.View()));
	mProjSpotlightFX->GetVariableByName("gLightRangeRcp")->AsScalar()->SetFloat(1.0f / range);
	mProjSpotlightFX->GetVariableByName("gLightIntensity")->AsScalar()->SetFloat(1.0f);

	//// Test to check if the camera is inside light volume.
	//bool inside = false;
	//XMVECTOR tipToPoint = mCamera.GetPositionXM() - XMLoadFloat3(&position);
	//// Project the vector from cone tip to test point onto light direction
	//// to find the point's distance along the axis.
	//XMVECTOR coneDist = XMVector3Dot(tipToPoint, directionXM);
	//// Reject values outside 0 <= coneDist <= coneHeight
	//if (0 <= XMVectorGetX(coneDist) && XMVectorGetX(coneDist) <= range)
	//{
	//	// Radius at point.
	//	float radiusAtPoint = (XMVectorGetX(coneDist) / range) * xyScale;
	//	// Calculate the point's orthogonal distance to axis and compare to
	//	// cone radius (radius at point).
	//	XMVECTOR pointOrthDirection = tipToPoint - XMVectorGetX(coneDist) * directionXM;
	//	XMVECTOR orthDistSq = XMVector3Dot(pointOrthDirection, pointOrthDirection);

	//	inside = (XMVectorGetX(orthDistSq) < radiusAtPoint * radiusAtPoint);
	//}
	//
	//// If we are inside the light volume, draw the cone's inside face.
	//if (inside)
	//{
	//	pd3dImmediateContext->RSSetState( mCullFront );
	//}
	//else
	//	pd3dImmediateContext->RSSetState( mCullBack );

	mProjSpotlightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	mConeModel->Render( pd3dImmediateContext );

	//// Reset culling mode back to normal, else other meshes might not render correctly.
	//if (inside)
	//	pd3dImmediateContext->RSSetState( mCullBack );
	
	mProjSpotlightFX->GetVariableByName("gProjLightTex")->AsShaderResource()->SetResource(0);
	mProjSpotlightTech->GetPassByIndex( 0 )->Apply(0, pd3dImmediateContext);
}

void App::RenderLights( ID3D11DeviceContext *pd3dImmediateContext, float fTime )
{
	// Bind the light accumulation buffer as a render target. Using additive blending,
	// the contribution of every light will be summed and stored in this buffer.
	pd3dImmediateContext->OMSetRenderTargets(1, &mLightRT, mMainDepthDSVReadOnly);
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	pd3dImmediateContext->ClearRenderTargetView(mLightRT, clearColor);
	pd3dImmediateContext->OMSetBlendState(mAdditiveBlend, 0, 0xffffffff);
	
	// Common for every light
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.Proj()), mCamera.Proj());
	// projA and projB are projection constants based on camera near and far
	// clip planes, used to reconstruct view space position from depth in shaders.
	// projA = zf / (zf - zn)
	// projB = -zn * zf / (zf - zn)
	float projA = mCamera.Proj()._33;
	float projB = mCamera.Proj()._43;

	//
	// Directional light stuff
	//

	pd3dImmediateContext->OMSetDepthStencilState(mNoDepthTest, 0);
	pd3dImmediateContext->RSSetState(mCullBack);

	// Set shader variables common for every directional light
	mDirectionalLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
	mDirectionalLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mGBuffer->NormalSRV());
	mDirectionalLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
	mDirectionalLightFX->GetVariableByName("gInvProj")->AsMatrix()->SetMatrix((float*)&invProj);
	mDirectionalLightFX->GetVariableByName("gProjA")->AsScalar()->SetFloat(projA);
	mDirectionalLightFX->GetVariableByName("gProjB")->AsScalar()->SetFloat(projB);

	// Render directional lights
	RenderDirectionalLight( pd3dImmediateContext, XMFLOAT3(0.4f, 0.4f, 0.4f), XMFLOAT3(0, -1, 1) );

	// Unbind the G-Buffer textures
	mDirectionalLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
	mDirectionalLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mDirectionalLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mDirectionalLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	//
	// Render point lights
	//

	pd3dImmediateContext->OMSetDepthStencilState(mDepthGreaterEqual, 0);
	pd3dImmediateContext->RSSetState(mCullFront);

	// Set shader variables common for every point light
	mPointLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
	mPointLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mGBuffer->NormalSRV());
	mPointLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
	mPointLightFX->GetVariableByName("gProjA")->AsScalar()->SetFloat(projA);
	mPointLightFX->GetVariableByName("gProjB")->AsScalar()->SetFloat(projB);

	XMFLOAT3 colors[10];
	colors[0] = XMFLOAT3( 0.133f, 0.545f, 0.133f ); // ForestGreen
	colors[1] = XMFLOAT3( 0, 0, 1 ); // Blue
	colors[2] = XMFLOAT3( 1, 0.75294f, 0.796078f ); // Pink
	colors[3] = XMFLOAT3( 1, 1, 0 ); // Yellow
	colors[4] = XMFLOAT3( 1, 0.6470588f, 0 ); // Orange
	colors[5] = XMFLOAT3( 0, 0.5f, 0 ); // Green
	colors[6] = XMFLOAT3( 0.862745f, 0.078431f, 0.235294f ); // Crimson
	colors[7] = XMFLOAT3( 0.39215686f, 0.5843137f, 0.92941176f ); // CornFlowerBlue
	colors[8] = XMFLOAT3( 1, 0.843137f, 0 ); // Gold
	colors[9] = XMFLOAT3( 0.94117647f, 1, 0.94117647f ); // Honeydew

	float lightRadiusFirstSet = 12.0f;
	float lightRadiusSecondSet = 20.0f;
	float lightIntensityFirstSet = 2.0f;
	float lightIntensitySecondSet = 1.0f;

	for (UINT i = 0; i < 10; ++i)
	{
		XMFLOAT3 position = XMFLOAT3(sinf(i * XM_2PI / 10 + fTime), 0.3f, cosf(i * XM_2PI / 10 + fTime));
		XMFLOAT3 pos = XMFLOAT3(position.x * 20, position.y * 20, position.z * 20);

		RenderPointLight( pd3dImmediateContext, colors[i], pos, lightRadiusFirstSet, lightIntensityFirstSet );

		position = XMFLOAT3(cosf(i * XM_2PI / 10 + fTime), -0.6f, sinf(i * XM_2PI / 10 + fTime));
		pos = XMFLOAT3(position.x * 20, position.y * 20, position.z * 20);

		//RenderPointLight( pd3dImmediateContext, colors[i], pos, lightRadiusSecondSet, lightIntensitySecondSet );
	}

	// Unbind the G-Buffer textures
	mPointLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
	mPointLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mPointLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mPointLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	//
	// Render spotlights
	//

	pd3dImmediateContext->OMSetDepthStencilState(mDepthGreaterEqual, 0);
	pd3dImmediateContext->RSSetState(mCullFront);
	
	XMFLOAT3 color(0.0f, 1.0f, 0.0f);
	XMFLOAT3 position(0.0f, 10.0f, 0.0f);
	XMFLOAT3 direction(0.0f, 0.0f, 1.0f);
	float range = 15.0f;
	float outerAngleDeg = 20.0f; // Angle from center outwards.
	float innerAngleDeg = 10.0f;

	direction.x = sinf(static_cast<float>( fTime ));
	direction.z = cosf(static_cast<float>( fTime ));
	
	// Set shader variables common for every spotlight
	mSpotlightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
	mSpotlightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mGBuffer->NormalSRV());
	mSpotlightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);

	RenderSpotlight( pd3dImmediateContext, color, position, direction, range,
		outerAngleDeg, innerAngleDeg );

	// Unbind G-Buffer textures
	mSpotlightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
	mSpotlightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mSpotlightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mSpotlightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	//
	// Render capsule lights
	//

	pd3dImmediateContext->OMSetDepthStencilState(mNoDepthTest, 0);
	pd3dImmediateContext->RSSetState(mCullBack);

	// Set shader variables common for every capsule light
	mCapsuleLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
	mCapsuleLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mGBuffer->NormalSRV());
	mCapsuleLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
	mCapsuleLightFX->GetVariableByName("gInvProj")->AsMatrix()->SetMatrix((float*)&invProj);
	
	// Render capsule lights
	RenderCapsuleLight( pd3dImmediateContext, XMFLOAT3(1.0f, 0.0f, 0.0f), XMFLOAT3(0, 0.5f, 0),
		XMFLOAT3( 0, 0, 1 ), 1.0f, 8.0f );
	RenderCapsuleLight( pd3dImmediateContext, XMFLOAT3(0.0f, 1.0f, 0.0f), XMFLOAT3(0, 0.5f, 0),
		XMFLOAT3( -direction.x, direction.y, direction.z ), 2.0f, 8.0f );

	// Unbind the G-Buffer textures
	mCapsuleLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
	mCapsuleLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mCapsuleLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mCapsuleLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	//
	// Render projective point lights
	//

	pd3dImmediateContext->OMSetDepthStencilState(mDepthGreaterEqual, 0);
	pd3dImmediateContext->RSSetState(mCullFront);

	// Set shader variables common for every projective point light
	mProjPointLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
	mProjPointLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mGBuffer->NormalSRV());
	mProjPointLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
	mProjPointLightFX->GetVariableByName("gProjA")->AsScalar()->SetFloat(projA);
	mProjPointLightFX->GetVariableByName("gProjB")->AsScalar()->SetFloat(projB);

	RenderProjPointLight( pd3dImmediateContext, mProjPointLightColor, XMFLOAT3( 0, 10.0f, 0 ), 20.0f, 1.0f, fTime );

	// Unbind the G-Buffer textures
	mProjPointLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
	mProjPointLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mProjPointLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mProjPointLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	//
	// Render projective spotlights
	//

	pd3dImmediateContext->OMSetDepthStencilState(mDepthGreaterEqual, 0);
	pd3dImmediateContext->RSSetState(mCullFront);

	position = XMFLOAT3(0,5,0);
	direction.y = -1.0f;
	
	// Set shader variables common for every projective spotlight
	mProjSpotlightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mGBuffer->ColorSRV());
	mProjSpotlightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mGBuffer->NormalSRV());
	mProjSpotlightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);

	// Reuse position and stuff from previous spotlight
	RenderProjSpotlight( pd3dImmediateContext, mProjSpotlightColor, position, direction, range,
		outerAngleDeg, innerAngleDeg );

	// Unbind G-Buffer textures
	mProjSpotlightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
	mProjSpotlightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mProjSpotlightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mProjSpotlightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

	//
	// Final stuff
	//

	// Reset blend and rasterizer states to default
	pd3dImmediateContext->OMSetBlendState(0, 0, 0xffffffff);
	pd3dImmediateContext->RSSetState( 0 );
	pd3dImmediateContext->OMSetDepthStencilState(0, 0);
}