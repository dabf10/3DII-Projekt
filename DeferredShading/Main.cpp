//--------------------------------------------------------------------------------------
// File: Main.cpp
//
// Empty starting point for new Direct3D 11 applications
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//--------------------------------------------------------------------------------------
#include "App.h"

static App g_App;


//--------------------------------------------------------------------------------------
// Reject any D3D11 devices that aren't acceptable by returning false
//--------------------------------------------------------------------------------------
bool CALLBACK IsD3D11DeviceAcceptable( const CD3D11EnumAdapterInfo *AdapterInfo, UINT Output, const CD3D11EnumDeviceInfo *DeviceInfo,
                                       DXGI_FORMAT BackBufferFormat, bool bWindowed, void* pUserContext )
{
    return static_cast<App *>( pUserContext )->IsD3D11DeviceAcceptable( AdapterInfo,
		Output, DeviceInfo, BackBufferFormat, bWindowed );
}


//--------------------------------------------------------------------------------------
// Called right before creating a D3D9 or D3D11 device, allowing the app to modify the device settings as needed
//--------------------------------------------------------------------------------------
bool CALLBACK ModifyDeviceSettings( DXUTDeviceSettings* pDeviceSettings, void* pUserContext )
{
    return static_cast<App *>( pUserContext )->ModifyDeviceSettings( pDeviceSettings );
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that aren't dependant on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11CreateDevice( ID3D11Device* pd3dDevice, const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc,
                                      void* pUserContext )
{
    return static_cast<App *>( pUserContext )->OnD3D11CreateDevice( pd3dDevice,
		pBackBufferSurfaceDesc );
}


//--------------------------------------------------------------------------------------
// Create any D3D11 resources that depend on the back buffer
//--------------------------------------------------------------------------------------
HRESULT CALLBACK OnD3D11ResizedSwapChain( ID3D11Device* pd3dDevice, IDXGISwapChain* pSwapChain,
                                          const DXGI_SURFACE_DESC* pBackBufferSurfaceDesc, void* pUserContext )
{
    return static_cast<App *>( pUserContext )->OnD3D11ResizedSwapChain( pd3dDevice,
		pSwapChain, pBackBufferSurfaceDesc );
}


//--------------------------------------------------------------------------------------
// Handle updates to the scene.  This is called regardless of which D3D API is used
//--------------------------------------------------------------------------------------
void CALLBACK OnFrameMove( double fTime, float fElapsedTime, void* pUserContext )
{
	static_cast<App *>( pUserContext )->OnFrameMove( fTime, fElapsedTime );
}


//--------------------------------------------------------------------------------------
// Render the scene using the D3D11 device
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11FrameRender( ID3D11Device* pd3dDevice, ID3D11DeviceContext* pd3dImmediateContext,
                                  double fTime, float fElapsedTime, void* pUserContext )
{
    static_cast<App *>( pUserContext )->OnD3D11FrameRender( pd3dDevice,
		pd3dImmediateContext, fTime, fElapsedTime );
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11ResizedSwapChain 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11ReleasingSwapChain( void* pUserContext )
{
	static_cast<App *>( pUserContext )->OnD3D11ReleasingSwapChain( );
}


//--------------------------------------------------------------------------------------
// Release D3D11 resources created in OnD3D11CreateDevice 
//--------------------------------------------------------------------------------------
void CALLBACK OnD3D11DestroyDevice( void* pUserContext )
{
	static_cast<App *>( pUserContext )->OnD3D11DestroyDevice( );
}


//--------------------------------------------------------------------------------------
// Handle messages to the application
//--------------------------------------------------------------------------------------
LRESULT CALLBACK MsgProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                          bool* pbNoFurtherProcessing, void* pUserContext )
{
    return static_cast<App *>( pUserContext )->MsgProc( hWnd, uMsg, wParam,
		lParam, pbNoFurtherProcessing );
}


//--------------------------------------------------------------------------------------
// Handle key presses
//--------------------------------------------------------------------------------------
void CALLBACK OnKeyboard( UINT nChar, bool bKeyDown, bool bAltDown, void* pUserContext )
{
	static_cast<App *>( pUserContext )->OnKeyboard( nChar, bKeyDown, bAltDown );
}


//--------------------------------------------------------------------------------------
// Handle mouse button presses
//--------------------------------------------------------------------------------------
void CALLBACK OnMouse( bool bLeftButtonDown, bool bRightButtonDown, bool bMiddleButtonDown,
                       bool bSideButton1Down, bool bSideButton2Down, int nMouseWheelDelta,
                       int xPos, int yPos, void* pUserContext )
{
	static_cast<App *>( pUserContext )->OnMouse( bLeftButtonDown, bRightButtonDown,
		bMiddleButtonDown, bSideButton1Down, bSideButton2Down, nMouseWheelDelta,
		xPos, yPos );
}


//--------------------------------------------------------------------------------------
// Call if device was removed.  Return true to find a new device, false to quit
//--------------------------------------------------------------------------------------
bool CALLBACK OnDeviceRemoved( void* pUserContext )
{
    return static_cast<App *>( pUserContext )->OnDeviceRemoved( );
}


//--------------------------------------------------------------------------------------
// Initialize everything and go into a render loop
//--------------------------------------------------------------------------------------
int WINAPI wWinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow )
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	//_CrtSetBreakAlloc(195681);
#endif

    // Set general DXUT callbacks
    DXUTSetCallbackFrameMove( OnFrameMove, &g_App );
    DXUTSetCallbackKeyboard( OnKeyboard, &g_App );
    DXUTSetCallbackMouse( OnMouse, false, &g_App );
    DXUTSetCallbackMsgProc( MsgProc, &g_App );
    DXUTSetCallbackDeviceChanging( ModifyDeviceSettings, &g_App );
    DXUTSetCallbackDeviceRemoved( OnDeviceRemoved, &g_App );

    // Set the D3D11 DXUT callbacks.
    DXUTSetCallbackD3D11DeviceAcceptable( IsD3D11DeviceAcceptable, &g_App );
    DXUTSetCallbackD3D11DeviceCreated( OnD3D11CreateDevice, &g_App );
    DXUTSetCallbackD3D11SwapChainResized( OnD3D11ResizedSwapChain, &g_App );
    DXUTSetCallbackD3D11FrameRender( OnD3D11FrameRender, &g_App );
    DXUTSetCallbackD3D11SwapChainReleasing( OnD3D11ReleasingSwapChain, &g_App );
    DXUTSetCallbackD3D11DeviceDestroyed( OnD3D11DestroyDevice, &g_App );

    // Perform any application-level initialization here
	g_App.Init( );

    DXUTInit( true, true, NULL ); // Parse the command line, show msgboxes on error, no extra command line params
    DXUTSetCursorSettings( true, true ); // Show the cursor and clip it when in full screen
    DXUTCreateWindow( L"DeferredShading" );

    // Only require 10-level hardware
    DXUTCreateDevice( D3D_FEATURE_LEVEL_11_0, true, 1280, 720 );
    DXUTMainLoop(); // Enter into the DXUT ren  der loop

    // Perform any application-level cleanup here

    return DXUTGetExitCode();
}


