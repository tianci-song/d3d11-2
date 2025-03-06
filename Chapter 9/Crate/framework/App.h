#pragma once

#include "GameTimer.h"
#include "d3dUtil.h"

#if defined(DEBUG) || defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#endif


class App 
{
protected:
	App(HINSTANCE instanceHandle);
	App(const App&) = delete;
	App& operator=(const App&) = delete;
	virtual ~App();

public:
	int Run();
	static App* Get();
	float AspectRatio() const;
	LRESULT MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

	virtual bool Initialize();

protected:
	virtual void Update(const GameTimer& gt);
	virtual void Draw(const GameTimer& gt) = 0;
	virtual void OnResize();

	virtual void OnLButtonDown(WPARAM btnState, int x, int y);
	virtual void OnLButtonUp(WPARAM btnState, int x, int y);
	virtual void OnMButtonDown(WPARAM btnState, int x, int y);
	virtual void OnMButtonUp(WPARAM btnState, int x, int y);
	virtual void OnRButtonDown(WPARAM btnState, int x, int y);
	virtual void OnRButtonUp(WPARAM btnState, int x, int y);
	virtual void OnMouseMove(WPARAM btnState, int x, int y);
	virtual void OnMouseScroll(WPARAM wParam, int x, int y);

private:
	void OnLMBButtonDown();
	void OnLMBButtonUp();
	void OnKeyDown();
	void OnKeyUp();
	void UpdateCamera();
	
	std::unordered_map<char, int> mKeyMap;
	const std::unordered_map<char, int>
		mMoveCommandMap = {
		{ 'w',  1 },
		{ 's', -1 },
		{ 'a', -1 },
		{ 'd',  1 },
		{ 'q', -1 },
		{ 'e',  1 },
	};

protected:
	bool InitWindows();	
	bool EnablePixGpuCapturer();	// Loading .dll file when debugging with PIX on Windows.
	bool InitDirect3D();
	void CalculateFrameStats();

	void CreateCommandObjects();
	void CreateSwapChain();
	void CreateRtvAndDsvDescriptorHeaps();

	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

protected:
	static App* mApp;

	HINSTANCE mInstanceHandle;
	HWND mhMainWnd = 0;
	GameTimer mTimer;

	static const int SwapChainBufferCount = 2;
	int mCurrBackBuffer = 0;
	ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
	ComPtr<ID3D12Resource> mDepthStencilBuffer;
	
	ComPtr<IDXGIFactory4> mdxgiFactory;		// Create swap chain
	ComPtr<IDXGISwapChain> mSwapChain;
	ComPtr<ID3D12Device> md3dDevice;		// Create command queue, command allocator, command list, fence and get the size of descriptor

	ComPtr<ID3D12Fence> mFence;
	UINT64 mCurrentFence = 0;

	ComPtr<ID3D12CommandQueue> mCommandQueue;
	ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;
	ComPtr<ID3D12GraphicsCommandList> mCommandList;

	ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	ComPtr<ID3D12DescriptorHeap> mDsvHeap;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap;
	ComPtr<ID3D12DescriptorHeap> mSrvHeap;

	UINT mRtvDescriptorSize = 0;
	UINT mDsvDescriptorSize = 0;
	UINT mCbvSrvUavDescriptorSize = 0;

	bool      mMinimized = false;  // is the application minimized?
	bool      mMaximized = false;  // is the application maximized?
	bool      mResizing = false;   // are the resize bars being dragged?
	bool      mFullscreenState = false;// fullscreen enabled

	// Set true to use 4X MSAA (?.1.8).  The default is false.
	bool      m4xMsaaState = false;    // 4X MSAA enabled
	UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

	D3D12_VIEWPORT mScreenViewport = {};
	D3D12_RECT mScissorRect = {};

	std::wstring mMainWndCaption = L"d3d App";
	DXGI_FORMAT mBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT mDepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	int mClientWidth = 800;
	int mClientHeight = 600;

	XMMATRIX mWorld = XMMatrixIdentity();
	XMMATRIX mView = XMMatrixIdentity();
	XMMATRIX mProj = XMMatrixIdentity();
	XMMATRIX mWorldViewProj = XMMatrixIdentity();
	POINT mLastCursorPosOfWindow{};
	POINT mLastCursorPosOfScreen{};

	// Camera params
	float mRadius = 5.f;
	float mYaw = 0.f;
	float mPitch = 0.f;
	bool  mIsOrbit = false;
	float mCameraRotSpeed = 0.2f;
	float mCameraMoveSpeed = 0.001f;
	XMVECTOR mCameraPos{ 0.f, 0.f, -3.f, 1.f };
	XMVECTOR mPivotPos{ 0.f, 0.f, 0.f, 1.f };
	XMFLOAT3 mLookAtDir{ 0.f, 0.f, 1.f };
	XMFLOAT3 mLateralDir{ 1.f, 0.f, 0.f };
	XMFLOAT3 mAxisOffset{ 0.f, 0.f, 0.f };

	// Screen params
	float mFov = XM_PIDIV4;
};