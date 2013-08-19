#ifndef _APP_H_
#define _APP_H_

#include "DXUT.h"
#include "DXUTgui.h"
#include "SDKmisc.h"
#include "DXUTsettingsdlg.h"
#include "Model.h"
#include "Camera.h"
#include <Windows.h>
#include <xnamath.h>
#include "d3dx11effect.h"

#ifdef _DEBUG
#pragma comment(lib, "Effect_x86\\Effects11D.lib")
#else
#pragma comment(lib, "Effect_x86\\Effects11.lib")
#endif

class App
{
public:
	enum
	{
		IDC_TOGGLEFULLSCREEN,
		IDC_CHANGEDEVICE,
	};

public:
	// Constructor/destructor
	App( );
	~App( );
	
	//
	// DXUT callback methods
	//

	void OnD3D11DestroyDevice( );
	void OnD3D11ReleasingSwapChain( );
	void OnFrameMove( double fTime, float fElapsedTime );
	void OnD3D11FrameRender( ID3D11Device* pd3dDevice,
		ID3D11DeviceContext* pd3dImmediateContext, double fTime, float fElapsedTime );
	void OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown );
	void OnMouse( bool bLeftButtonDown, bool bRightButtonDown, bool bMiddleButtonDown,
		bool bSideButton1Down, bool bSideButton2Down, int nMouseWheelDelta, int xPos, int yPos );

	bool IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo,
		UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo, DXGI_FORMAT BackBufferFormat, bool bWindowed );
	bool ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings );
	bool OnDeviceRemoved( );

	HRESULT OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc );
	HRESULT OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
		const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc );
	LRESULT MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
		bool* pbNoFurtherProcessing );
	
	void OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl *pControl );
	// Trampoline function to call a method as a callback function given a user context.
	static void CALLBACK OnGUIEvent( UINT nEvent, int nControlID, CDXUTControl *pControl, void *pUserContext )
	{
		static_cast<App *>( pUserContext )->OnGUIEvent( nEvent, nControlID, pControl );
	}

	//
	// Non DXUT methods
	//

	bool Init( );

private:
	struct ModelMaterial
	{
		XMFLOAT4 Ambient;
		XMFLOAT4 Diffuse;
		XMFLOAT4 Specular;
		XMFLOAT3 TransmissionFilter;
		float OpticalDensity;
	};

	void OnMouseMove(WPARAM btnState, int x, int y);

	bool BuildVertexLayout(ID3D11Device *device);
	bool BuildFX(ID3D11Device *device);

	void RenderText();

	HRESULT CreateGBuffer( ID3D11Device *device, UINT width, UINT height );
	//void RenderDirectionalLight( XMFLOAT3 color, XMFLOAT3 direction );
	//void RenderPointLight( XMFLOAT3 color, XMFLOAT3 position, float radius, float intensity );
	//void RenderSpotlight( XMFLOAT3 color, XMFLOAT3 position, XMFLOAT3 direction, float radius, float intensity, float angleCosine, float decayExponent );

	void Blur( ID3D11ShaderResourceView *inputSRV,
		ID3D11RenderTargetView *outputRTV, bool horizontalBlur,
		ID3D11DeviceContext *d3dImmediateContext );

private:
	Model *mModel;
	XMFLOAT4X4 mBthWorld[2];
	std::vector<UINT> mBthMaterialToUseForGroup;
	std::vector<OBJLoader::SurfaceMaterial> mBthMaterials;
	std::vector<ID3D11ShaderResourceView*> mMeshSRV;

	Model *mSphereModel;
	XMFLOAT4X4 mSphereWorld;
	std::vector<UINT> mSphereMaterialToUseForGroup;
	std::vector<OBJLoader::SurfaceMaterial> mSphereMaterials;
	std::vector<ID3D11ShaderResourceView*> mSphereSRV;

	Model *mConeModel;
	XMFLOAT4X4 mConeWorld;
	std::vector<UINT> mConeMaterialToUseForGroup;
	std::vector<OBJLoader::SurfaceMaterial> mConeMaterials;

	Camera mCamera;

	POINT mLastMousePos;

	ID3D11Buffer *mFloorVB;
	ID3D11ShaderResourceView *mFloorTex;
	XMFLOAT4X4 mFloorWorld;

	ID3D11InputLayout *mInputLayout;

	CDXUTDialog mHUD; // Manages the 3D UI
	CD3DSettingsDlg mD3DSettingsDlg; // Device settings dialog
	CDXUTDialogResourceManager mDialogResourceManager; // Manager for shared resources of dialogs
	
	CDXUTTextHelper *mTxtHelper;

	ID3D11Buffer *mQuadVB;
	ID3D11InputLayout *mPosTexInputLayout;
	ID3DX11Effect *mQuadFX;
	ID3DX11EffectTechnique *mQuadTech;
	
	ID3D11RenderTargetView *mColorRT;
	ID3D11ShaderResourceView *mColorSRV;
	ID3D11RenderTargetView *mNormalRT;
	ID3D11ShaderResourceView *mNormalSRV;
	ID3D11RenderTargetView *mLightRT;
	ID3D11ShaderResourceView *mLightSRV;
	ID3D11DepthStencilView *mMainDepthDSV;
	ID3D11ShaderResourceView *mMainDepthSRV;
	ID3D11RenderTargetView *mAOMapRT;
	ID3D11ShaderResourceView *mAOMapSRV;
	ID3D11RenderTargetView *mAOIntermediateBlurRT;
	ID3D11ShaderResourceView *mAOIntermediateBlurSRV;
	ID3D11RenderTargetView *mCompositeRT;
	ID3D11ShaderResourceView *mCompositeSRV;

	ID3D11ShaderResourceView *mRandomNormalsSRV;

	ID3DX11Effect *mFillGBufferFX;
	ID3DX11EffectTechnique *mFillGBufferTech;
	ID3DX11Effect *mClearGBufferFX;
	ID3DX11EffectTechnique *mClearGBufferTech;

	ID3DX11Effect *mDirectionalLightFX;
	ID3DX11EffectTechnique *mDirectionalLightTech;
	ID3DX11Effect *mPointLightFX;
	ID3DX11EffectTechnique *mPointLightTech;
	ID3DX11Effect *mSpotlightFX;
	ID3DX11EffectTechnique *mSpotlightTech;

	ID3DX11Effect *mCombineLightFX;
	ID3DX11EffectTechnique *mCombineLightTech;
	ID3DX11Effect *mAOMapFX;
	ID3DX11EffectTechnique *mAOMapTech;
	ID3DX11Effect *mBilateralBlurFX;
	ID3DX11Effect *mOldFilmFX;
	ID3DX11EffectTechnique *mOldFilmTech;

	ID3D11DepthStencilState *mNoDepthWrite;
	ID3D11BlendState *mAdditiveBlend;
	ID3D11RasterizerState *mCullBack;
	ID3D11RasterizerState *mCullFront;
	ID3D11RasterizerState *mCullNone;

	const DXGI_SURFACE_DESC *mBackBufferSurfaceDesc;

	D3D11_VIEWPORT mQuarterSizeViewport;
};

#endif // _APP_H_