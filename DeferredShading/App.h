#ifndef _APP_H_
#define _APP_H_

#include "DXUT.h"
#include "DXUTgui.h"
#include "SDKmisc.h"
#include "DXUTsettingsdlg.h"
#include "Model.h"
#include "AnimatedModel.h"
#include "Camera.h"
#include "SSAO.h"
#include "GBuffer.h"
#include <Windows.h>
#include <xnamath.h>
#include "d3dx11effect.h"
#include "SkinnedData.h"
#include "GnomeImporter.h"
#include "PostProcessRT.h"


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
	void OnMouseMove(WPARAM btnState, int x, int y);

	bool BuildVertexLayout( ID3D11Device *device );
	bool BuildFX( ID3D11Device *device );
	bool CompileShader( ID3D11Device *device, const char *filename, ID3DX11Effect **fx );

	void RenderText();

	HRESULT CreateGBuffer( ID3D11Device *device, UINT width, UINT height );
	void RenderLights( ID3D11DeviceContext *pd3dImmediateContext, float fTime );
	void RenderDirectionalLight( ID3D11DeviceContext *pd3dImmediateContext,
		XMFLOAT3 color, XMFLOAT3 direction );
	void RenderPointLight( ID3D11DeviceContext *pd3dImmediateContext, XMFLOAT3 color,
		XMFLOAT3 position, float radius, float intensity );
	void RenderSpotlight( ID3D11DeviceContext *pd3dImmediateContext, XMFLOAT3 color,
		XMFLOAT3 position, XMFLOAT3 direction, float range, float outerAngleDeg,
		float innerAngleDeg );
	void RenderCapsuleLight( ID3D11DeviceContext *pd3dImmediateContext, XMFLOAT3 color,
		XMFLOAT3 position, XMFLOAT3 direction, float range, float length );
	void RenderProjPointLight( ID3D11DeviceContext *pd3dImmediateContext, ID3D11ShaderResourceView *tex,
		XMFLOAT3 position, float radius, float intensity, float fTime ); // Remove time, it's just to test light animation
	void RenderProjSpotlight( ID3D11DeviceContext *pd3dImmediateContext, ID3D11ShaderResourceView *tex,
		XMFLOAT3 position, XMFLOAT3 direction, float range, float outerAngleDeg,
		float innerAngleDeg );

private:
	Model *mBth;
	XMFLOAT4X4 mBthWorld;
	ID3D11ShaderResourceView *mBthColor;

	Model *mLevel;
	XMFLOAT4X4 mLevelWorld;
	ID3D11ShaderResourceView *mLevelSRV[3];

	Model *mSphereModel;
	XMFLOAT4X4 mSphereWorld;
	ID3D11ShaderResourceView *mSphereSRV;

	Model *mConeModel;
	XMFLOAT4X4 mConeWorld;

	AnimatedModel* mFlamingoModel;
	XMFLOAT4X4 mFlamingoWorld;
	ID3D11ShaderResourceView* mFlamingoColor;

	ID3D11ShaderResourceView *mProjPointLightColor;
	ID3D11ShaderResourceView *mProjSpotlightColor;
	
	SkinnedData *mAnimatedModel;

	Camera mCamera;

	POINT mLastMousePos;

	ID3D11InputLayout *mInputLayout;

	CDXUTDialog mHUD; // Manages the 3D UI
	CD3DSettingsDlg mD3DSettingsDlg; // Device settings dialog
	CDXUTDialogResourceManager mDialogResourceManager; // Manager for shared resources of dialogs
	
	CDXUTTextHelper *mTxtHelper;

	// Accumulates lights
	ID3D11RenderTargetView *mLightRT;
	ID3D11ShaderResourceView *mLightSRV;
	// Regular depth buffer (we create it ourselves because we use it as SRV)
	ID3D11DepthStencilView *mMainDepthDSV;
	ID3D11DepthStencilView *mMainDepthDSVReadOnly;
	ID3D11ShaderResourceView *mMainDepthSRV;

	ID3DX11Effect *mFullscreenTextureFX;

	ID3DX11Effect *mFillGBufferFX;

	ID3DX11Effect *mDirectionalLightFX;
	ID3DX11EffectTechnique *mDirectionalLightTech;
	ID3DX11Effect *mPointLightFX;
	ID3DX11EffectTechnique *mPointLightTech;
	ID3DX11Effect *mSpotlightFX;
	ID3DX11EffectTechnique *mSpotlightTech;
	ID3DX11Effect *mCapsuleLightFX;
	ID3DX11EffectTechnique *mCapsuleLightTech;
	ID3DX11Effect *mProjPointLightFX;
	ID3DX11EffectTechnique *mProjPointLightTech;
	ID3DX11Effect *mProjSpotlightFX;
	ID3DX11EffectTechnique *mProjSpotlightTech;

	ID3DX11Effect *mCombineLightFX;
	ID3DX11Effect *mOldFilmFX;

	ID3D11DepthStencilState *mNoDepthTest;
	ID3D11DepthStencilState *mDepthGreaterEqual;
	ID3D11BlendState *mAdditiveBlend;
	ID3D11RasterizerState *mCullBack;
	ID3D11RasterizerState *mCullFront;
	ID3D11RasterizerState *mCullNone;

	const DXGI_SURFACE_DESC *mBackBufferSurfaceDesc;

	SSAO *mSSAO;

	GBuffer *mGBuffer;

	PostProcessRT *mPostProcessRT;
};

#endif // _APP_H_