/*
	==================================================
	Fler intressanta saker att prova på
	==================================================
	Använda djupbuffer som en G-Buffer http://mynameismjp.wordpress.com/2010/09/05/position-from-depth-3/
	Istället för att använda en sfär som volym för spotlight (fungerar dock säkert helt ok)
	är det kanske rimligt med en kon (så kan man synligt rendera volymen om man vill).
	Läs MJP:s inlägg http://www.gamedev.net/topic/533058-deferred-renderer-and-spot-light-volume/
	och om jag ber TA om hjälp, ta gärna med normal och texcoord och glöm ej: LH! Ta även
	reda på hur orientering kan skötas; godtyckliga rotationer med eulervinklar är inte alltid så roligt.
	http://social.msdn.microsoft.com/Forums/en-US/f4d80d25-c947-440e-832c-fdae6cfc8b51/vector-to-rotation-ie-point-models-forward-to-their-direction
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
	mQuadVB( 0 ),
	mPosTexInputLayout( 0 ),
	mQuadFX( 0 ),
	mQuadTech( 0 ),
	mColorRT( 0 ),
	mColorSRV( 0 ),
	mNormalRT( 0 ),
	mNormalSRV( 0 ),
	mLightRT( 0 ),
	mLightSRV( 0 ),
	mMainDepthDSV( 0 ),
	mMainDepthSRV( 0 ),
	mAOMapRT( 0 ),
	mAOMapSRV( 0 ),
	mAOIntermediateBlurRT( 0 ),
	mAOIntermediateBlurSRV( 0 ),
	mCompositeRT( 0 ),
	mCompositeSRV( 0 ),
	mRandomNormalsSRV( 0 ),
	mFillGBufferFX( 0 ),
	mFillGBufferTech( 0 ),
	mClearGBufferFX( 0 ),
	mClearGBufferTech( 0 ),
	mDirectionalLightFX( 0 ),
	mDirectionalLightTech( 0 ),
	mPointLightFX( 0 ),
	mPointLightTech( 0 ),
	mSpotlightFX( 0 ),
	mSpotlightTech( 0 ),
	mCombineLightFX( 0 ),
	mCombineLightTech( 0 ),
	mAOMapFX( 0 ),
	mAOMapTech( 0 ),
	mBilateralBlurFX( 0 ),
	mOldFilmFX( 0 ),
	mNoDepthWrite( 0 ),
	mAdditiveBlend( 0 ),
	mCullBack( 0 ),
	mCullFront( 0 ),
	mCullNone( 0 )
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

	V_RETURN( D3DX11CreateShaderResourceViewFromFileA(pd3dDevice, "noise.png", 0, 0, &mRandomNormalsSRV, 0) );

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

	V_RETURN( D3DX11CreateShaderResourceViewFromFileA( pd3dDevice, "floor.jpg", 0, 0, &mFloorTex, 0 ) );

	//
	// Full screen quad
	//

	vbDesc.ByteWidth = 6 * 20;
	vbDesc.StructureByteStride = 20;

	struct PosTex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Tex;
	};

	PosTex quadVerts[6] =
	{
		{ XMFLOAT3(-1, 1, 0), XMFLOAT2(0, 0) },
		{ XMFLOAT3(1, 1, 0), XMFLOAT2(1, 0) },
		{ XMFLOAT3(-1, -1, 0), XMFLOAT2(0, 1) },

		{ XMFLOAT3(-1, -1, 0), XMFLOAT2(0, 1) },
		{ XMFLOAT3(1, 1, 0), XMFLOAT2(1, 0) },
		{ XMFLOAT3(1, -1, 0), XMFLOAT2(1, 1) },
	};

	vinit.pSysMem = quadVerts;
	V_RETURN( pd3dDevice->CreateBuffer( &vbDesc, &vinit, &mQuadVB ) );

	D3D11_DEPTH_STENCIL_DESC dsDesc = D3D11_DEPTH_STENCIL_DESC( );
	dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthEnable = true;
	dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	V_RETURN( pd3dDevice->CreateDepthStencilState( &dsDesc, &mNoDepthWrite ) );

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

	SAFE_RELEASE( mRandomNormalsSRV );
	
	SAFE_RELEASE( mInputLayout );

	SAFE_RELEASE( mFloorVB );
	SAFE_RELEASE( mFloorTex );

	SAFE_RELEASE( mQuadFX );
	SAFE_RELEASE( mPosTexInputLayout );
	SAFE_RELEASE( mQuadVB );

	SAFE_RELEASE( mFillGBufferFX );
	SAFE_RELEASE( mClearGBufferFX );

	SAFE_RELEASE( mDirectionalLightFX );
	SAFE_RELEASE( mPointLightFX );
	SAFE_RELEASE( mSpotlightFX );

	SAFE_RELEASE( mCombineLightFX );
	SAFE_RELEASE( mAOMapFX );
	SAFE_RELEASE( mBilateralBlurFX );
	SAFE_RELEASE( mOldFilmFX );

	SAFE_RELEASE( mNoDepthWrite );
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
	//pDeviceSettings->d3d11.AutoCreateDepthStencil = false;

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

	mQuarterSizeViewport.Width = pBackBufferSurfaceDesc->Width / 2;
	mQuarterSizeViewport.Height = pBackBufferSurfaceDesc->Height / 2;

	return S_OK;
}

//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void App::OnD3D11ReleasingSwapChain( )
{
	mDialogResourceManager.OnD3D11ReleasingSwapChain();
	
	SAFE_RELEASE( mColorRT );
	SAFE_RELEASE( mColorSRV );
	SAFE_RELEASE( mNormalRT );
	SAFE_RELEASE( mNormalSRV );
	SAFE_RELEASE( mLightRT );
	SAFE_RELEASE( mLightSRV );
	SAFE_RELEASE( mMainDepthDSV );
	SAFE_RELEASE( mMainDepthSRV );
	SAFE_RELEASE( mAOMapRT );
	SAFE_RELEASE( mAOMapSRV );
	SAFE_RELEASE( mAOIntermediateBlurRT );
	SAFE_RELEASE( mAOIntermediateBlurSRV );
	SAFE_RELEASE( mCompositeRT );
	SAFE_RELEASE( mCompositeSRV );
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

	// Width and height are set when window is resized.
	mQuarterSizeViewport.TopLeftX = 0.0f;
	mQuarterSizeViewport.TopLeftY = 0.0f;
	mQuarterSizeViewport.MinDepth = 0.0f;
	mQuarterSizeViewport.MaxDepth = 1.0f;

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

	uniformScaleFactor = 10.0f;
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

	D3D11_VIEWPORT fullViewport;
	UINT numViewports = 1;
	pd3dImmediateContext->RSGetViewports(&numViewports, &fullViewport);

	// If the settings dialog is being shown, then render it instead of rendering
	// the app's scene.
	if (mD3DSettingsDlg.IsActive() )
	{
		mD3DSettingsDlg.OnRender( fElapsedTime );
		return;
	}

	pd3dImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D11RenderTargetView *rtvs[] = { mColorRT, mNormalRT };
	pd3dImmediateContext->OMSetRenderTargets( 2, rtvs, 0 );

	//
	// Clear G-Buffer by rendering a full-screen quad using correct shader.
	//
	{
		UINT strides = 20;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers(0, 1, &mQuadVB, &strides, &offsets);
		pd3dImmediateContext->IASetInputLayout(mPosTexInputLayout);

		D3DX11_TECHNIQUE_DESC techDesc;
		mClearGBufferTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			mClearGBufferTech->GetPassByIndex(p)->Apply(0, pd3dImmediateContext);
			pd3dImmediateContext->Draw(6, 0);
		}
	}
	
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
			XMMATRIX wvp = world * mCamera.ViewProj();
			XMMATRIX worldInvTranspose = XMMatrixInverse(&XMMatrixDeterminant(world), XMMatrixTranspose(world));

			// Set object specific constants.
			mFillGBufferFX->GetVariableByName("gWorldInvTranspose")->AsMatrix()->SetMatrix((float*)&worldInvTranspose);
			mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);

			// Loop through every submesh the current mesh consists of.
			for (UINT s = 0; s < mModel->SubMeshes(); ++s)
			{
				mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mMeshSRV[mBthMaterialToUseForGroup[s]]);

				D3DX11_TECHNIQUE_DESC techDesc;
				mFillGBufferTech->GetDesc(&techDesc);
				for (UINT p = 0; p < techDesc.Passes; ++p)
				{
					mFillGBufferTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
					mModel->RenderSubMesh( pd3dImmediateContext, s );
				}
			}
		}

		//
		// Floor
		//

		XMMATRIX world = XMLoadFloat4x4( &mFloorWorld );
		XMMATRIX wvp = world * mCamera.ViewProj();
		XMMATRIX worldInvTranspose = XMMatrixInverse(&XMMatrixDeterminant(world), XMMatrixTranspose(world));

		// Set per object constants.
		mFillGBufferFX->GetVariableByName("gWorldInvTranspose")->AsMatrix()->SetMatrix((float*)&worldInvTranspose);
		mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);
		mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mFloorTex);

		UINT strides = 32;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers( 0, 1, &mFloorVB, &strides, &offsets );

		D3DX11_TECHNIQUE_DESC techDesc;
		mFillGBufferTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			mFillGBufferTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
			pd3dImmediateContext->Draw( 6, 0 );
		}

		mFillGBufferTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		//
		// Sphere model
		//
		
		world = XMLoadFloat4x4(&mSphereWorld);
		wvp = world * mCamera.ViewProj();
		worldInvTranspose = XMMatrixInverse(&XMMatrixDeterminant(world), XMMatrixTranspose(world));

		// Set object specific constants.
		mFillGBufferFX->GetVariableByName("gWorldInvTranspose")->AsMatrix()->SetMatrix((float*)&worldInvTranspose);
		mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);

		// Loop through every submesh the current mesh consists of.
		for (UINT s = 0; s < mSphereModel->SubMeshes(); ++s)
		{
			mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mSphereSRV[mSphereMaterialToUseForGroup[s]]);

			D3DX11_TECHNIQUE_DESC techDesc;
			mFillGBufferTech->GetDesc(&techDesc);
			for (UINT p = 0; p < techDesc.Passes; ++p)
			{
				mFillGBufferTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
				mSphereModel->RenderSubMesh( pd3dImmediateContext, s );
			}
		}

		//
		// Cone model
		//

		world = XMLoadFloat4x4(&mConeWorld);
		wvp = world * mCamera.ViewProj();
		worldInvTranspose = XMMatrixInverse(&XMMatrixDeterminant(world), XMMatrixTranspose(world));

		// Set object specific constants.
		mFillGBufferFX->GetVariableByName("gWorldInvTranspose")->AsMatrix()->SetMatrix((float*)&worldInvTranspose);
		mFillGBufferFX->GetVariableByName("gWVP")->AsMatrix()->SetMatrix((float*)&wvp);
		mFillGBufferFX->GetVariableByName("gDiffuseMap")->AsShaderResource()->SetResource(mMeshSRV[0]);

		mFillGBufferTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			mFillGBufferTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
			mConeModel->Render( pd3dImmediateContext );
		}
	}

	// Bind the light accumulation buffer as a render target. Using additive blending,
	// the contribution of every light will be summed and stored in this buffer.
	pd3dImmediateContext->OMSetRenderTargets(1, &mLightRT, 0);
	pd3dImmediateContext->ClearRenderTargetView(mLightRT, clearColor);

	//
	// Render a directional light as a full-screen post-process.
	//
	{
		UINT strides = 20;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers(0, 1, &mQuadVB, &strides, &offsets);
		pd3dImmediateContext->IASetInputLayout(mPosTexInputLayout);
		pd3dImmediateContext->OMSetBlendState(mAdditiveBlend, 0, 0xffffffff);

		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.ViewProj()), mCamera.ViewProj());
		mDirectionalLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mColorSRV);
		mDirectionalLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mNormalSRV);
		mDirectionalLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
		mDirectionalLightFX->GetVariableByName("gCameraPosition")->AsVector()->SetFloatVector((float*)&mCamera.GetPosition());
		mDirectionalLightFX->GetVariableByName("gInvViewProj")->AsMatrix()->SetMatrix((float*)&invViewProj);

		XMFLOAT3 dir[3] = { XMFLOAT3(0, -1, 1), XMFLOAT3(-1, 0, 1), XMFLOAT3(1, 0, 1) };
		XMFLOAT3 col[3] = { XMFLOAT3(0.4f, 0.4f, 0.4f), XMFLOAT3(1, 0.3f, 0.15f), XMFLOAT3(0, 0.5f, 1) };
		
		D3DX11_TECHNIQUE_DESC techDesc;
		mDirectionalLightTech->GetDesc(&techDesc);

		//for (UINT l = 0; l < 3; ++l)
		for (UINT l = 0; l < 1; ++l)
		{
			mDirectionalLightFX->GetVariableByName("gLightDirection")->AsVector()->SetFloatVector((float*)&dir[l]);
			mDirectionalLightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&col[l]);

			for (UINT p = 0; p < techDesc.Passes; ++p)
			{
				mDirectionalLightTech->GetPassByIndex(p)->Apply(0, pd3dImmediateContext);
				pd3dImmediateContext->Draw(6, 0);
			}
		}

		// Unbind the G-Buffer textures
		mDirectionalLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
		mDirectionalLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
		mDirectionalLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
		mDirectionalLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		// Reset blend state to default
		pd3dImmediateContext->OMSetBlendState(0, 0, 0xffffffff);
	}

	//
	// Render a point light
	// TODO: Would be nice to use depth buffer to depth-test light volumes. But
	// that means the depth buffer would need to be bound both as dsv and srv at
	// the same time. Direct3D11 should support this I believe.
	//
	{
		pd3dImmediateContext->OMSetBlendState(mAdditiveBlend, 0, 0xffffffff);
		
		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.ViewProj()), mCamera.ViewProj());

		// G-Buffer parameters
		mPointLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mColorSRV);
		mPointLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mNormalSRV);
		mPointLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
		mPointLightFX->GetVariableByName("gView")->AsMatrix()->SetMatrix((float*)&mCamera.View());
		mPointLightFX->GetVariableByName("gProjection")->AsMatrix()->SetMatrix((float*)&mCamera.Proj());
		mPointLightFX->GetVariableByName("gCameraPosition")->AsVector()->SetFloatVector((float*)&mCamera.GetPosition());
		mPointLightFX->GetVariableByName("gInvViewProj")->AsMatrix()->SetMatrix((float*)&invViewProj);

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

		float angle = static_cast<float>( fTime );

		float lightRadiusFirstSet = 12.0f;
		float lightRadiusSecondSet = 20.0f;
		float lightIntensityFirstSet = 2.0f;
		float lightIntensitySecondSet = 1.0f;

		for (UINT i = 0; i < 10; ++i)
		{
			XMFLOAT3 position = XMFLOAT3(sinf(i * XM_2PI / 10 + fTime), 0.3f, cosf(i * XM_2PI / 10 + fTime));
			XMFLOAT3 pos = XMFLOAT3(position.x * 20, position.y * 20, position.z * 20);

			// Compute the light world matrix. Scale according to light radius,
			// and translate it to light position.
			XMMATRIX sphereWorld = XMMatrixScaling(lightRadiusFirstSet, lightRadiusFirstSet, lightRadiusFirstSet) *
				XMMatrixTranslation(pos.x, pos.y, pos.z);

			mPointLightFX->GetVariableByName("gWorld")->AsMatrix()->SetMatrix((float*)&sphereWorld);
			mPointLightFX->GetVariableByName("gLightPosition")->AsVector()->SetFloatVector((float*)&XMFLOAT3(pos.x, pos.y, pos.z));
			mPointLightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&colors[i]);
			mPointLightFX->GetVariableByName("gLightRadius")->AsScalar()->SetFloat(lightRadiusFirstSet);
			mPointLightFX->GetVariableByName("gLightIntensity")->AsScalar()->SetFloat(lightIntensityFirstSet);
		
			// Calculate the distance between the camera and light center.
			XMVECTOR vCameraToCenter = mCamera.GetPositionXM() - XMLoadFloat3(&pos);
			XMVECTOR cameraToCenterSq = XMVector3Dot(vCameraToCenter, vCameraToCenter);

			// If we are inside the light volume, draw the sphere's inside face
			if (XMVectorGetX(cameraToCenterSq) < lightRadiusFirstSet * lightRadiusFirstSet)
				pd3dImmediateContext->RSSetState( mCullFront );
			else
				pd3dImmediateContext->RSSetState( mCullBack );

			// Render the light volume.
			D3DX11_TECHNIQUE_DESC techDesc;
			mPointLightTech->GetDesc( &techDesc );
			for (UINT p = 0; p< techDesc.Passes; ++p)
			{
				mPointLightTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
				mSphereModel->Render( pd3dImmediateContext );
			}

			position = XMFLOAT3(cosf(i * XM_2PI / 10 + fTime), -0.6f, sinf(i * XM_2PI / 10 + fTime));
			pos = XMFLOAT3(position.x * 20, position.y * 20, position.z * 20);

			// Compute the light world matrix. Scale according to light radius,
			// and translate it to light position.
			sphereWorld = XMMatrixScaling(lightRadiusSecondSet, lightRadiusSecondSet, lightRadiusSecondSet) *
				XMMatrixTranslation(pos.x, pos.y, pos.z);

			mPointLightFX->GetVariableByName("gWorld")->AsMatrix()->SetMatrix((float*)&sphereWorld);
			mPointLightFX->GetVariableByName("gLightPosition")->AsVector()->SetFloatVector((float*)&XMFLOAT3(pos.x, pos.y, pos.z));
			mPointLightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&colors[i]);
			mPointLightFX->GetVariableByName("gLightRadius")->AsScalar()->SetFloat(lightRadiusSecondSet);
			mPointLightFX->GetVariableByName("gLightIntensity")->AsScalar()->SetFloat(lightIntensitySecondSet);

			// Calculate the distance between the camera and light center.
			vCameraToCenter = mCamera.GetPositionXM() - XMLoadFloat3(&pos);
			cameraToCenterSq = XMVector3Dot(vCameraToCenter, vCameraToCenter);

			// If we are inside the light volume, draw the sphere's inside face
			if (XMVectorGetX(cameraToCenterSq) < lightRadiusSecondSet * lightRadiusSecondSet)
				pd3dImmediateContext->RSSetState( mCullFront );
			else
				pd3dImmediateContext->RSSetState( mCullBack );

			// Render the light volume.
			for (UINT p = 0; p < techDesc.Passes; ++p)
			{
				mPointLightTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
				mSphereModel->Render( pd3dImmediateContext );
			}
		}

		// Unbind shader resource views
		mPointLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
		mPointLightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
		mPointLightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
		mPointLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		// Reset blend and rasterizer states to default
		pd3dImmediateContext->OMSetBlendState(0, 0, 0xffffffff);
		pd3dImmediateContext->RSSetState( 0 );
	}

	//
	// Render a spotlight
	//
	{
		pd3dImmediateContext->OMSetBlendState(mAdditiveBlend, 0, 0xffffffff);

		float lightRadius = 15.0f;
		float lightIntensity = 1.0f;
		float angleCosine = cosf(XMConvertToRadians(10));
		float decayExponent = 1.0f;
		XMFLOAT3 lightColor(0.0f, 0.4f, 0.0f);
		XMFLOAT3 lightPosition(0.0f, 10.0f, 0.0f);
		XMVECTOR direction = XMVectorSet(0, 0, 1, 0);
		direction = XMVectorSetX(direction, sinf(static_cast<float>( fTime )));
		direction = XMVectorSetZ(direction, cosf(static_cast<float>( fTime )));
		direction = XMVector3Normalize(direction);
		
		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.ViewProj()), mCamera.ViewProj());

		// G-Buffer parameters
		mSpotlightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mColorSRV);
		mSpotlightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mNormalSRV);
		mSpotlightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
		mSpotlightFX->GetVariableByName("gView")->AsMatrix()->SetMatrix((float*)&mCamera.View());
		mSpotlightFX->GetVariableByName("gProjection")->AsMatrix()->SetMatrix((float*)&mCamera.Proj());
		mSpotlightFX->GetVariableByName("gCameraPosition")->AsVector()->SetFloatVector((float*)&mCamera.GetPosition());
		mSpotlightFX->GetVariableByName("gInvViewProj")->AsMatrix()->SetMatrix((float*)&invViewProj);
		
		float xyScale = tanf(XMConvertToRadians(10)) * lightRadius;
		XMMATRIX coneWorld = XMMatrixScaling(xyScale, xyScale, lightRadius) *
			XMMatrixTranslation(lightPosition.x, lightPosition.y, lightPosition.z);

		mSpotlightFX->GetVariableByName("gWorld")->AsMatrix()->SetMatrix((float*)&coneWorld);
		mSpotlightFX->GetVariableByName("gLightPosition")->AsVector()->SetFloatVector((float*)&lightPosition);
		mSpotlightFX->GetVariableByName("gDirection")->AsVector()->SetFloatVector((float*)&direction);
		mSpotlightFX->GetVariableByName("gLightColor")->AsVector()->SetFloatVector((float*)&lightColor);
		mSpotlightFX->GetVariableByName("gLightRadius")->AsScalar()->SetFloat(lightRadius);
		mSpotlightFX->GetVariableByName("gAngleCosine")->AsScalar()->SetFloat(angleCosine);
		mSpotlightFX->GetVariableByName("gDecayExponent")->AsScalar()->SetFloat(decayExponent);
		mSpotlightFX->GetVariableByName("gLightIntensity")->AsScalar()->SetFloat(lightIntensity);

		bool inside = false;
		float baseRadius = tanf(XMConvertToRadians(10)) * lightRadius;
		XMVECTOR tipToPoint = mCamera.GetPositionXM() - XMLoadFloat3(&lightPosition);
		// Project the vector from cone tip to test point onto light direction
		// to find the point's distance along the axis.
		XMVECTOR coneDist = XMVector3Dot(tipToPoint, direction);
		// Reject values outside 0 <= coneDist <= coneHeight
		if (0 <= XMVectorGetX(coneDist) && XMVectorGetX(coneDist) <= lightRadius)
		{
			// Radius at point.
			float radiusAtPoint = (XMVectorGetX(coneDist) / lightRadius) * baseRadius;
			// Calculate the point's orthogonal distance to axis and compare to
			// cone radius (radius at point).
			XMVECTOR pointOrthDirection = tipToPoint - XMVectorGetX(coneDist) * direction;
			XMVECTOR orthDistSq = XMVector3Dot(pointOrthDirection, pointOrthDirection);

			inside = (XMVectorGetX(orthDistSq) < radiusAtPoint * radiusAtPoint);
		}

		// If we are inside the light volume, draw the cone's inside face.
		if (inside)
		{
			pd3dImmediateContext->RSSetState( mCullFront );
		}
		else
			pd3dImmediateContext->RSSetState( mCullBack );

		// Render the light volume.
		D3DX11_TECHNIQUE_DESC techDesc;
		mSpotlightTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			mSpotlightTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
			mConeModel->Render( pd3dImmediateContext );
		}

		// Unbind shader resources
		mSpotlightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
		mSpotlightFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
		mSpotlightFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
		mSpotlightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		// Reset blend and rasterizer states to default
		pd3dImmediateContext->OMSetBlendState( 0, 0, 0xffffffff );
		pd3dImmediateContext->RSSetState( 0 );
	}

	pd3dImmediateContext->OMSetRenderTargets(1, &mAOMapRT, 0);
	pd3dImmediateContext->ClearRenderTargetView(mAOMapRT, clearColor);
	pd3dImmediateContext->RSSetViewports(1, &mQuarterSizeViewport);

	//
	// SSAO
	//
	{
		UINT strides = 20;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers(0, 1, &mQuadVB, &strides, &offsets);
		pd3dImmediateContext->IASetInputLayout(mPosTexInputLayout);

		//
		// Hemispherical using Poisson-Disk sampling in screen-space
		//

		//XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.ViewProj()), mCamera.ViewProj());
		//XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(mCamera.View()), mCamera.View());
		//float distanceThreshold = 1.0f;
		//float radius = 0.02f;
		//XMFLOAT2 filterRadius(radius, radius);

		//mAOMapFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mNormalSRV);
		//mAOMapFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
		//mAOMapFX->GetVariableByName("gInvViewProjection")->AsMatrix()->SetMatrix((float*)&invViewProj);
		//mAOMapFX->GetVariableByName("gInvView")->AsMatrix()->SetMatrix((float*)&invView);
		//mAOMapFX->GetVariableByName("gDistanceThreshold")->AsScalar()->SetFloat(distanceThreshold);
		//mAOMapFX->GetVariableByName("gFilterRadius")->AsVector()->SetFloatVector((float*)&filterRadius);

		//
		// My implementation
		//
		
		float offset = 18.0f;
		float aoStart = 0.1f;
		float hemisphereRadius = 1.0f;

		XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.Proj()), mCamera.Proj());

		mAOMapFX->GetVariableByName("gRandomNormals")->AsShaderResource()->SetResource(mRandomNormalsSRV);
		mAOMapFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mNormalSRV);
		mAOMapFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
		mAOMapFX->GetVariableByName("gOffset")->AsScalar()->SetFloat(offset);
		mAOMapFX->GetVariableByName("gAOStart")->AsScalar()->SetFloat(aoStart);
		mAOMapFX->GetVariableByName("gHemisphereRadius")->AsScalar()->SetFloat(hemisphereRadius);
		mAOMapFX->GetVariableByName("gInvViewProjection")->AsMatrix()->SetMatrix((float*)&invViewProj);
		mAOMapFX->GetVariableByName("gView")->AsMatrix()->SetMatrix((float*)&mCamera.View());
		mAOMapFX->GetVariableByName("gProjection")->AsMatrix()->SetMatrix((float*)&mCamera.Proj());
		
		//
		// Hemispherical using noise map from gamerendering.com
		//

		//float totStrength = 1.38f;
		//float strength = 0.07f;
		//float offset = 18.0f;
		//float falloff = 0.000002f;
		//float rad = 0.006f;

		//XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(mCamera.Proj()), mCamera.Proj());

		//mAOMapFX->GetVariableByName("gRandomNormals")->AsShaderResource()->SetResource(mRandomNormalsSRV);
		//mAOMapFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource(mNormalSRV);
		//mAOMapFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource(mMainDepthSRV);
		//mAOMapFX->GetVariableByName("gTotStrength")->AsScalar()->SetFloat(totStrength);
		//mAOMapFX->GetVariableByName("gStrength")->AsScalar()->SetFloat(strength);
		//mAOMapFX->GetVariableByName("gOffset")->AsScalar()->SetFloat(offset);
		//mAOMapFX->GetVariableByName("gFalloff")->AsScalar()->SetFloat(falloff);
		//mAOMapFX->GetVariableByName("gRad")->AsScalar()->SetFloat(rad);
		//mAOMapFX->GetVariableByName("gInvViewProjection")->AsMatrix()->SetMatrix((float*)&invViewProj);
		//mAOMapFX->GetVariableByName("gView")->AsMatrix()->SetMatrix((float*)&mCamera.View());

		D3DX11_TECHNIQUE_DESC techDesc;
		mAOMapTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			mAOMapTech->GetPassByIndex(p)->Apply(0, pd3dImmediateContext);
			pd3dImmediateContext->Draw(6, 0);
		}
		
		mAOMapFX->GetVariableByName("gRandomNormals")->AsShaderResource()->SetResource( 0 );
		mAOMapFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
		mAOMapFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
		mAOMapTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		// Blur AO map n times.
		for (int i = 0; i < 1; ++i)
		{
			Blur( mAOMapSRV, mAOIntermediateBlurRT, true, pd3dImmediateContext );
			Blur( mAOIntermediateBlurSRV, mAOMapRT, false, pd3dImmediateContext );
		}
	}

	pd3dImmediateContext->RSSetViewports(1, &fullViewport);

	// Render to back buffer.
	pd3dImmediateContext->OMSetRenderTargets( 1, &rtv, 0 );

	//
	// Render a full-screen quad that combines the light from the light map
	// with the color map from the G-Buffer
	//
	{
		UINT strides = 20;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers(0, 1, &mQuadVB, &strides, &offsets);
		pd3dImmediateContext->IASetInputLayout(mPosTexInputLayout);

		mCombineLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource(mColorSRV);
		mCombineLightFX->GetVariableByName("gLightMap")->AsShaderResource()->SetResource(mLightSRV);

		D3DX11_TECHNIQUE_DESC techDesc;
		mCombineLightTech->GetDesc(&techDesc);
		for (UINT p = 0; p < techDesc.Passes; ++p)
		{
			mCombineLightTech->GetPassByIndex(p)->Apply(0, pd3dImmediateContext);
			pd3dImmediateContext->Draw(6, 0);
		}

		// Unbind shader resources
		mCombineLightFX->GetVariableByName("gColorMap")->AsShaderResource()->SetResource( 0 );
		mCombineLightFX->GetVariableByName("gLightMap")->AsShaderResource()->SetResource( 0 );
		mCombineLightTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	}

	//
	// Generate an old-film looking effect
	//
	//{
	//	UINT strides = 20;
	//	UINT offsets = 0;
	//	pd3dImmediateContext->IASetVertexBuffers(0, 1, &mQuadVB, &strides, &offsets);
	//	pd3dImmediateContext->IASetInputLayout(mPosTexInputLayout);

	//	float sepia = 0.6f;
	//	float noise = 0.2f;
	//	float scratch = 0.3f;
	//	float innerVignetting;
	//	float outerVignetting;
	//	float random;
	//	float timeLapse;

	//	mOldFilmFX->GetVariableByName("gCompositeImage")->AsShaderResource()->SetResource(mCompositeSRV);
	//	mOldFilmFX->GetVariableByName("gSepiaValue")->AsScalar()->SetFloat(sepia);
	//	mOldFilmFX->GetVariableByName("gNoiseValue")->AsScalar()->SetFloat(noise);
	//	mOldFilmFX->GetVariableByName("gScratchValue")->AsScalar()->SetFloat(scratch);
	//	mOldFilmFX->GetVariableByName("gInnerVignetting")->AsScalar()->SetFloat(innerVignetting);
	//	mOldFilmFX->GetVariableByName("gOuterVignetting")->AsScalar()->SetFloat(outerVignetting);
	//	mOldFilmFX->GetVariableByName("gRandomValue")->AsScalar()->SetFloat(random);
	//	mOldFilmFX->GetVariableByName("gTimeLapse")->AsScalar()->SetFloat(timeLapse);

	//	D3DX11_TECHNIQUE_DESC techDesc;
	//	mOldFilmTech->GetDesc(&techDesc);
	//	for (UINT p = 0; p < techDesc.Passes; ++p)
	//	{
	//		mOldFilmTech->GetPassByIndex(p)->Apply( 0, pd3dImmediateContext );
	//		pd3dImmediateContext->Draw(6, 0);
	//	}

	//	// Unbind shader resources
	//	mOldFilmFX->GetVariableByName("gCompositeImage")->AsShaderResource()->SetResource( 0 );
	//	mOldFilmTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );
	//}

	//
	// Full-screen textured quad
	//

	// Specify 1 or 4.
	UINT numImages = 0;
	if (numImages)
	{
		// The first code is just to easily display 1 full screen image or
		// 4 smaller in quadrants. Simply select what resource views to use
		// and how many of those to draw.
		ID3D11ShaderResourceView *srvs[4] = {
			mAOMapSRV,
			mNormalSRV,
			mLightSRV,
			mColorSRV
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

		UINT strides = 20;
		UINT offsets = 0;
		pd3dImmediateContext->IASetVertexBuffers(0, 1, &mQuadVB, &strides, &offsets);
		pd3dImmediateContext->IASetInputLayout(mPosTexInputLayout);

		for (int i = 0; i < numImages; ++i)
		{
			ID3DX11EffectTechnique *tech = 0;
			if (srvs[i] == mAOMapSRV || srvs[i] == mMainDepthSRV)
				tech = mQuadFX->GetTechniqueByName("SingleChannel");
			else
				tech = mQuadFX->GetTechniqueByName("MultiChannel");
			
			D3DX11_TECHNIQUE_DESC techDesc;
			tech->GetDesc(&techDesc);

			pd3dImmediateContext->RSSetViewports(1, &vp[i]);
			mQuadFX->GetVariableByName("gTexture")->AsShaderResource()->SetResource(srvs[i]);

			tech->GetPassByIndex(0)->Apply(0, pd3dImmediateContext);
			pd3dImmediateContext->Draw(6, 0);
		}

		mQuadFX->GetVariableByName("gTexture")->AsShaderResource()->SetResource( 0 );
		mQuadTech->GetPassByIndex( 0 )->Apply( 0, pd3dImmediateContext );

		pd3dImmediateContext->RSSetViewports(1, &fullViewport);
	}

	//
	// Render the UI
	//

	mHUD.OnRender( fElapsedTime );
	RenderText();
}

void App::Blur( ID3D11ShaderResourceView *inputSRV,
	ID3D11RenderTargetView *outputRTV, bool horizontalBlur,
	ID3D11DeviceContext *d3dImmediateContext )
{
	ID3D11RenderTargetView *rtv[1] = { outputRTV };
	d3dImmediateContext->OMSetRenderTargets( 1, rtv, 0 );

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	d3dImmediateContext->ClearRenderTargetView( outputRTV, clearColor );

	mBilateralBlurFX->GetVariableByName("gTexelWidth")->AsScalar()->SetFloat(1.0f / (mBackBufferSurfaceDesc->Width / 2));
	mBilateralBlurFX->GetVariableByName("gTexelHeight")->AsScalar()->SetFloat(1.0f / (mBackBufferSurfaceDesc->Height / 2));
	mBilateralBlurFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( mNormalSRV );
	mBilateralBlurFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( mMainDepthSRV );
	mBilateralBlurFX->GetVariableByName("gImageToBlur")->AsShaderResource()->SetResource( inputSRV );

	ID3DX11EffectTechnique *tech;
	if (horizontalBlur)
		tech = mBilateralBlurFX->GetTechniqueByName("HorizontalBlur");
	else
		tech = mBilateralBlurFX->GetTechniqueByName("VerticalBlur");

	// Input layout and vertex buffer already set.
	D3DX11_TECHNIQUE_DESC techDesc;
	tech->GetDesc( &techDesc );
	for (UINT p = 0; p < techDesc.Passes; ++p)
	{
		tech->GetPassByIndex(p)->Apply( 0, d3dImmediateContext );
		d3dImmediateContext->Draw( 6, 0 );
	}
	
	mBilateralBlurFX->GetVariableByName("gNormalMap")->AsShaderResource()->SetResource( 0 );
	mBilateralBlurFX->GetVariableByName("gDepthMap")->AsShaderResource()->SetResource( 0 );
	mBilateralBlurFX->GetVariableByName("gImageToBlur")->AsShaderResource()->SetResource( 0 );
	tech->GetPassByIndex( 0 )->Apply( 0, d3dImmediateContext );
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
	DWORD shaderFlags = D3D10_SHADER_ENABLE_STRICTNESS;
#if defined(DEBUG) || defined(_DEBUG)
	shaderFlags |= D3D10_SHADER_DEBUG;
	//shaderFlags |= D3D10_SHADER_SKIP_OPTIMIZATION;
#endif

	ID3D10Blob *compiledShader;
	ID3D10Blob *errorMsgs;
	HRESULT hr;

	//
	// Full-screen quad
	//

	hr = D3DX11CompileFromFileA("Shaders/FullScreenQuadTest.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mQuadFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mQuadTech = mQuadFX->GetTechniqueByIndex(0);

	//
	// GBuffer
	//

	hr = D3DX11CompileFromFileA("Shaders/GBuffer.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mFillGBufferFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mFillGBufferTech = mFillGBufferFX->GetTechniqueByIndex(0);

	//
	// ClearGBuffer
	//

	hr = D3DX11CompileFromFileA("Shaders/ClearGBuffer.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mClearGBufferFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mClearGBufferTech = mClearGBufferFX->GetTechniqueByIndex(0);

	//
	// DirectionalLight
	//

	hr = D3DX11CompileFromFileA("Shaders/DirectionalLight.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mDirectionalLightFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mDirectionalLightTech = mDirectionalLightFX->GetTechniqueByIndex(0);

	//
	// CombineLight
	//

	hr = D3DX11CompileFromFileA("Shaders/CombineLight.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mCombineLightFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mCombineLightTech = mCombineLightFX->GetTechniqueByIndex(0);

	//
	// PointLight
	//

	hr = D3DX11CompileFromFileA("Shaders/PointLight.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mPointLightFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mPointLightTech = mPointLightFX->GetTechniqueByIndex(0);

	//
	// Spotlight
	//

	hr = D3DX11CompileFromFileA("Shaders/Spotlight.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mSpotlightFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mSpotlightTech = mSpotlightFX->GetTechniqueByIndex(0);

	//
	// SSAO
	//

	hr = D3DX11CompileFromFileA("Shaders/SSAO.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mAOMapFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mAOMapTech = mAOMapFX->GetTechniqueByIndex(0);

	//
	// BilateralBlur
	//

	hr = D3DX11CompileFromFileA("Shaders/BilateralBlur.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mBilateralBlurFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	//
	// OldFilm
	//

	hr = D3DX11CompileFromFileA("Shaders/OldFilm.fx", 0, 0, "", "fx_5_0",
		shaderFlags, 0, 0, &compiledShader, &errorMsgs, 0);

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
		compiledShader->GetBufferSize(), 0, device, &mOldFilmFX))) return false;

	// Done with compiled shader.
	SAFE_RELEASE(compiledShader);

	mOldFilmTech = mOldFilmFX->GetTechniqueByIndex( 0 );

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
	mFillGBufferTech->GetPassByIndex(0)->GetDesc(&passDesc);
	if (FAILED(device->CreateInputLayout(vertexDesc, 3, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &mInputLayout))) return false;

	//
	// PosTex
	//

	D3D11_INPUT_ELEMENT_DESC posTexDesc[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	mQuadTech->GetPassByIndex( 0 )->GetDesc( &passDesc );
	if (FAILED(device->CreateInputLayout(posTexDesc, 2, passDesc.pIAInputSignature,
		passDesc.IAInputSignatureSize, &mPosTexInputLayout))) return false;

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

	D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;

	//
	// Color
	//

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex ) );
	
	rtvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateRenderTargetView( tex, &rtvDesc, &mColorRT ) );

	srvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mColorSRV ) );
	
	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// Normal
	//

	//texDesc.Format = DXGI_FORMAT_R10G10B10A2_UNORM;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex ) );

	rtvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateRenderTargetView( tex, &rtvDesc, &mNormalRT ) );

	srvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mNormalSRV ) );
	
	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// Light (not actually G-Buffer, but used to accumulate light)
	//

	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex) );

	rtvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateRenderTargetView( tex, &rtvDesc, &mLightRT ) );

	srvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mLightSRV ) );
	
	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// Composite
	//

	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex ) );
	V_RETURN( device->CreateRenderTargetView( tex, 0, &mCompositeRT ) );
	V_RETURN( device->CreateShaderResourceView( tex, 0, &mCompositeSRV ) );

	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// Ambient Occlusion
	//
	
	texDesc.Width = mBackBufferSurfaceDesc->Width / 2;
	texDesc.Height = mBackBufferSurfaceDesc->Height / 2;
	texDesc.Format = DXGI_FORMAT_R16_UNORM;
	//texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex) );

	rtvDesc.Format = texDesc.Format;
	//V_RETURN( device->CreateRenderTargetView( tex, &rtvDesc, &mAOMapRT ) );
	V_RETURN( device->CreateRenderTargetView( tex, NULL, &mAOMapRT ) );

	srvDesc.Format = texDesc.Format;
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mAOMapSRV ) );
	
	// Views saves reference
	SAFE_RELEASE( tex );

	//
	// AO Intermediate Blur
	//
	
	V_RETURN( device->CreateTexture2D( &texDesc, 0, &tex) );
	V_RETURN( device->CreateRenderTargetView( tex, &rtvDesc, &mAOIntermediateBlurRT ) );
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mAOIntermediateBlurSRV ) );
	
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

	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	V_RETURN( device->CreateShaderResourceView( tex, &srvDesc, &mMainDepthSRV ) );

	// Views saves reference
	SAFE_RELEASE( tex );


	return S_OK;
}