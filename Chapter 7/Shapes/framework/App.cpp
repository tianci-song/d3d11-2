#include "App.h"

LRESULT CALLBACK
WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

App* App::mApp = nullptr;


App::App(HINSTANCE instanceHandle) :
    mInstanceHandle(instanceHandle)
{
    assert(mApp == nullptr);
    mApp = this;
}

App::~App()
{
    if (md3dDevice)
    {
        FlushCommandQueue();
    }
}

int App::Run()
{
    mTimer.Reset();

    MSG msg = { 0 };
    while (msg.message != WM_QUIT)
    {
        // If no message received, PeekMessage() returns 0.
        // If message is available, it returns non-zero.
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg); // Send WM_CHAR message if a character is pressed (except F1, F2...)
            DispatchMessage(&msg);
        }
        // Do the game logic, play the animation...
        else
        {
            mTimer.Tick();  // Tick has internal stopped? check.
            if (!mTimer.IsStopped())
            {
                CalculateFrameStats();
                Update(mTimer);
                Draw(mTimer);
            }
            else
            {
                Sleep(100);
            }
        }
    }
    return (int)msg.wParam;
}

App* App::Get()
{
    return mApp;
}

float App::AspectRatio() const
{
    if (mClientHeight != 0)
    {
        return static_cast<float>(mClientWidth) / mClientHeight;
    }
    
    return 1.f;
}

LRESULT App::MsgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Process message, need to return 0 when processed
    switch (msg)
    {
        // WM_ACTIVATE is sent when the window is activated or deactivated.  
        // We pause the game when the window is deactivated and unpause it 
        // when it becomes active.  
    case WM_ACTIVATE:
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            mTimer.Stop();
        }
        else
        {
            mTimer.Start();
        }
        return 0;

        // WM_SIZE is sent when the user resizes the window.  
    case WM_SIZE:
        // Save the new client area dimensions.
        mClientWidth = LOWORD(lParam);
        mClientHeight = HIWORD(lParam);
        if (md3dDevice)
        {
            if (wParam == SIZE_MINIMIZED)
            {
                mMinimized = true;
                mMaximized = false;
            }
            else if (wParam == SIZE_MAXIMIZED)
            {
                mMinimized = false;
                mMaximized = true;
                OnResize();
            }
            else if (wParam == SIZE_RESTORED)
            {
                // Restoring from minimized state?
                if (mMinimized)
                {
                    mMinimized = false;
                    OnResize();
                }

                // Restoring from maximized state?
                else if (mMaximized)
                {
                    mMaximized = false;
                    OnResize();
                }
                else if (mResizing)
                {
                    // If user is dragging the resize bars, we do not resize 
                    // the buffers here because as the user continuously 
                    // drags the resize bars, a stream of WM_SIZE messages are
                    // sent to the window, and it would be pointless (and slow)
                    // to resize for each WM_SIZE message received from dragging
                    // the resize bars.  So instead, we reset after the user is 
                    // done resizing the window and releases the resize bars, which 
                    // sends a WM_EXITSIZEMOVE message.
                }
                else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
                {
                    OnResize();
                }
            }
        }
        return 0;

        // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
    case WM_ENTERSIZEMOVE:
        mResizing = true;
        mTimer.Stop();
        return 0;

        // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
        // Here we reset everything based on the new window dimensions.
    case WM_EXITSIZEMOVE:
        mResizing = false;
        mTimer.Start();
        OnResize();
        return 0;

    case WM_KEYDOWN:
        OnKeyDown();
        return 0;
    case WM_KEYUP:
        OnKeyUp();
        return 0;

    case WM_LBUTTONDOWN:
        OnLButtonDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MBUTTONDOWN:
        OnMButtonDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONDOWN:
        OnRButtonDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MBUTTONUP:
        OnMButtonUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_RBUTTONUP:
        OnRButtonUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEWHEEL:
        OnMouseScroll(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

        // WM_DESTROY is sent when the window is being destroyed.
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Leave the resting unprocessed message to the default window process function
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

bool App::Initialize()
{
    if (!InitWindows()) { return false; }
    if (!InitDirect3D()) { return false; }

    OnResize();     // OnResize() also intialize the resource

    return true;
}

bool App::InitWindows()
{
    WNDCLASS wc = {};

    // Fill in the WNDCLASS struct
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = mInstanceHandle;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"BasicWndClass";

    // Register the window class instance in Windows system, so that we can create the window with that istance
    if (!RegisterClass(&wc))
    {
        MessageBox(0, L"RegisterClass FAILED", 0, 0);
        return false;
    }

    // Create the window and return the window handle so that the app/function can know which window it should process
    mhMainWnd = CreateWindow(
        L"BasicWndClass",    // Use the previous registered window class instance
        mMainWndCaption.c_str(),    // Window title
        WS_OVERLAPPEDWINDOW,    // Window sample
        CW_USEDEFAULT,  // Coordinate x
        CW_USEDEFAULT,  // Coordinate y
        mClientWidth,  // Window width
        mClientHeight,  // Window height
        0,  // Parent window
        0,  // Menu handle
        mInstanceHandle, // App instance handle
        0   // other params for creating window
    );

    if (mhMainWnd == 0)
    {
        MessageBox(0, L"CreateWindow FAILED", 0, 0);
        return false;
    }

    // ghMainWnd param is needed so the function can know which window it should show and update
    ShowWindow(mhMainWnd, true);
    UpdateWindow(mhMainWnd);

    return true;
}

bool App::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG) 
// Enable the D3D12 debug layer.
{
    ComPtr<ID3D12Debug> debugController;
    D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) ;
    debugController->EnableDebugLayer();
}
#endif
    
    CreateDXGIFactory2(
        DXGI_CREATE_FACTORY_DEBUG, 
        IID_PPV_ARGS(&mdxgiFactory)) >> chk;
    
    D3D12CreateDevice(
        nullptr, 
        D3D_FEATURE_LEVEL_11_0, 
        IID_PPV_ARGS(&md3dDevice)) >> chk ;
    
    md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)) >> chk;
    
    mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CreateCommandObjects();
    CreateSwapChain();
    CreateRtvAndDsvDescriptorHeaps();

    return true;
}

void App::Update(const GameTimer& gt)
{
    ProcessInput();

    UpdateCamera();

    mProj = XMMatrixPerspectiveFovLH(mFov, AspectRatio(), 1.f, 1000.f);

    mWorldViewProj = mWorld * mView * mProj;
}

void App::OnResize()
{
    assert(md3dDevice);
    assert(mSwapChain);
    assert(mDirectCmdListAlloc);

    // Flush before changing any resources.
    FlushCommandQueue();

    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr) >> chk;

    // Release the previous resources we will be recreating.
    for (int i = 0; i < SwapChainBufferCount; ++i)
        mSwapChainBuffer[i].Reset();
    mDepthStencilBuffer.Reset();

    // Resize the swap chain.
    mSwapChain->ResizeBuffers(
        SwapChainBufferCount,
        mClientWidth, mClientHeight,
        mBackBufferFormat,
        DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH) >> chk;

    mCurrBackBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])) >> chk;
        md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, mRtvDescriptorSize);
    }

    // Create the depth/stencil buffer and view.
    const D3D12_RESOURCE_DESC depthStencilDesc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
        .Alignment = 0,
        .Width = (UINT64)mClientWidth,
        .Height = (UINT)mClientHeight,
        .DepthOrArraySize = 1,
        .MipLevels = 1,

        // Correction 11/12/2016: SSAO chapter requires an SRV to the depth buffer to read from 
        // the depth buffer.  Therefore, because we need to create two views to the same resource:
        //   1. SRV format: DXGI_FORMAT_R24_UNORM_X8_TYPELESS
        //   2. DSV Format: DXGI_FORMAT_D24_UNORM_S8_UINT
        // we need to create the depth buffer resource with a typeless format.  
        .Format = DXGI_FORMAT_R24G8_TYPELESS,
        .SampleDesc = {
            .Count = m4xMsaaState ? 4u : 1u,
            .Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0u,
        },
        .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
        .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
    };

    const D3D12_CLEAR_VALUE optClear = {
        .Format = mDepthStencilFormat,
        .DepthStencil = {
            .Depth = 1.0f,
            .Stencil = 0,
        },
    };

    md3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(&mDepthStencilBuffer)) >> chk;

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    const D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {
        .Format = mDepthStencilFormat,
        .ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D,
        .Flags = D3D12_DSV_FLAG_NONE,
        .Texture2D = {.MipSlice = 0, },
    };

    md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    // Transition the resource from its initial state to be used as a depth buffer.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    // Execute the resize commands.
    mCommandList->Close() >> chk;
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until resize is complete.
    FlushCommandQueue();

    // Update the viewport transform to cover the client area.
    mScreenViewport = {
        .TopLeftX = 0.f,
        .TopLeftY = 0.f,
        .Width = static_cast<float>(mClientWidth),
        .Height = static_cast<float>(mClientHeight),
        .MinDepth = 0.0f,
        .MaxDepth = 1.0f,
    };

    mScissorRect = { 0, 0, mClientWidth, mClientHeight };
}

void App::OnKeyDown()
{
    for (const auto& c : mMoveCommandMap)
    {
        if (Input::IsKeyPressed(c.first))
        {
            mKeyMap[c.first] = c.second;
        }
    }
}

void App::OnKeyUp()
{
    for (const auto& c : mMoveCommandMap)
    {
        if (!Input::IsKeyPressed(c.first))
        {
            mKeyMap[c.first] = 0;
        }
    }
}

void App::ProcessInput()
{
    // Move the camera along the lookat/lateral/vertical direction.

    const float movingSpeed = 0.004f;
    mAxisOffset.x += movingSpeed * (
        (mKeyMap['w'] + mKeyMap['s']) * mLookAtDir.x +
        (mKeyMap['a'] + mKeyMap['d']) * mLateralDir.x);
    mAxisOffset.y += movingSpeed * (
        (mKeyMap['w'] + mKeyMap['s']) * mLookAtDir.y +
        (mKeyMap['a'] + mKeyMap['d']) * mLateralDir.y + 
        (mKeyMap['q'] + mKeyMap['e']) * 1.f );
    mAxisOffset.z += movingSpeed * (
        (mKeyMap['w'] + mKeyMap['s']) * mLookAtDir.z +
        (mKeyMap['a'] + mKeyMap['d']) * mLateralDir.z);
}

void App::UpdateCamera()
{
    // When pitch ranges (-pi/2, pi/2), upDir is 1, and when ranges (pi/2, 3pi/2), upDir is -1.
    // (If not change the direction, it will flash to an inverted scene)
    // The sin() satisfy the signal change in this area, so take it as the component.
    const float upComp = std::sin(mPitch + XM_PIDIV2) >= 0 ? 1.f : -1.f;
    XMVECTOR upDir = XMVectorSet(0.f, upComp, 0.f, 0.f);

    // Must rotate around asix-X first, then rotate around axis-Y !!!
    // If the camera orbits the pivot (pitch, yaw), it equals camera rotate itself (-pitch, -yaw).
    XMVECTOR lookAtDir = XMVector4Transform(
        XMVectorSet(0.f, 0.f, 1.f, 0.f),
        XMMatrixRotationX(mPitch) * XMMatrixRotationY(mYaw));
    
    // To calculate the lateral movement.
    XMVECTOR lateralDir = XMVector3Normalize(
        XMVector3Cross(
            XMVectorSet(0.f, 1.f, 0.f, 0.f),
            lookAtDir));
    
    // Save as XMFLOAT3, make the calculation easier.
    XMStoreFloat3(&mLookAtDir, lookAtDir);
    XMStoreFloat3(&mLateralDir, lateralDir);

    // Check if the camera is rotating itself or orbiting around the pivot.
    // For self-rot, camera pos is unchanged, for orbit, pivot pos is unchanged.
    if (mIsOrbit)
    {
        mCameraPos = XMVectorSubtract(mPivotPos, XMVectorScale(lookAtDir, mRadius));
    }
    else
    {
        mPivotPos = XMVectorAdd(mCameraPos, XMVectorScale(lookAtDir, mRadius));
    }

    // It's amazing that rotation before or after translation are all correct. (needs more math proof)
    // Change the order of rotation around the XYZ axis and translation will get
    // the different result, but here the camera rotates around the pivot or rotate itself,
    // rather than the XYZ axis, however can get the same result.
    XMMATRIX trans = XMMatrixTranslationFromVector(XMLoadFloat3(&mAxisOffset));

    mView = XMMatrixLookAtLH(
        XMVector4Transform(mCameraPos, trans),
        XMVector4Transform(mPivotPos, trans),
        upDir);
}

void App::OnLButtonDown(WPARAM btnState, int x, int y)
{  
    // Ensure LButton is the first button pressed before RButton and MButton,
    // so that OnLMBButtonDown won't be done twice to override the previous one.
    if ((btnState & (MK_RBUTTON | MK_MBUTTON)) == 0)
    {
        OnLMBButtonDown();
    }
}

void App::OnLButtonUp(WPARAM btnState, int x, int y)
{
    // Ensure no RButton or MButton are still pressed.
    if ((btnState & (MK_RBUTTON | MK_MBUTTON)) == 0)
    {
        OnLMBButtonUp();
    }
}

void App::OnMButtonDown(WPARAM btnState, int x, int y)
{
    if ((btnState & (MK_LBUTTON | MK_RBUTTON)) == 0)
    {
        OnLMBButtonDown();
    }
}

void App::OnMButtonUp(WPARAM btnState, int x, int y)
{
    if ((btnState & (MK_LBUTTON | MK_RBUTTON)) == 0)
    {
        OnLMBButtonUp();
    }
}

void App::OnRButtonDown(WPARAM btnState, int x, int y)
{
    if ((btnState & (MK_LBUTTON | MK_MBUTTON)) == 0)
    {
        OnLMBButtonDown();
    }
}

void App::OnRButtonUp(WPARAM btnState, int x, int y)
{
    if ((btnState & (MK_LBUTTON | MK_MBUTTON)) == 0)
    {
        OnLMBButtonUp();
    }
}

void App::OnMouseMove(WPARAM btnState, int x, int y)
{  
    const float dx = XMConvertToRadians(static_cast<float>
        (x - mLastCursorPosOfWindow.x));
    const float dy = XMConvertToRadians(static_cast<float>
        (y - mLastCursorPosOfWindow.y));

    const float rotSpeed = 0.2f;
    mIsOrbit =
        btnState == MK_LBUTTON && Input::IsKeyPressed(VK_MENU) ? true : false;

    switch (btnState)
    {
    case MK_LBUTTON | MK_RBUTTON:
    case MK_MBUTTON | MK_RBUTTON:
    case MK_MBUTTON:
        mAxisOffset.x += dx * mLateralDir.x;
        mAxisOffset.y += -dy;
        mAxisOffset.z += dx * mLateralDir.z;
        break;
    case MK_LBUTTON:
        if (Input::IsKeyPressed(VK_MENU)) {
            mPitch += dy * rotSpeed;
            mYaw += dx * rotSpeed;
        }
        else {
            mYaw += dx * rotSpeed;
            mAxisOffset.x += -dy * mLookAtDir.x;
            mAxisOffset.z += -dy * mLookAtDir.z;
        }
        break;
    case MK_RBUTTON:
        if (Input::IsKeyPressed(VK_MENU)) {
            mAxisOffset.x += (dy + dx) * mLookAtDir.x;
            mAxisOffset.y += (dy + dx) * mLookAtDir.y;
            mAxisOffset.z += (dy + dx) * mLookAtDir.z;
        }
        else {
            mPitch += dy * rotSpeed;
            mYaw += dx * rotSpeed;
        }
        break;
    }

    mLastCursorPosOfWindow = { x, y };
}

void App::OnMouseScroll(WPARAM wParam, int x, int y)
{
    const float zDelta = (float)GET_WHEEL_DELTA_WPARAM(wParam);
    const float sensitivity = 5.f;

    mFov = MathHelper::Clamp(
        mFov - sensitivity * XMConvertToRadians(zDelta / WHEEL_DELTA),
        0.001f, 
        XM_PI - 0.1f);
}

void App::OnLMBButtonDown()
{
    // Capture mouse input for the specific window, so that the input
    // can still work even if the cursor is outside the window.
    SetCapture(mhMainWnd);

    // Hide the cursor and save its current pos.
    GetCursorPos(&mLastCursorPosOfScreen);
    ShowCursor(false);
}

void App::OnLMBButtonUp()
{
    // Release the mouse capture.
    ReleaseCapture();

    // Restore the cursor.
    SetCursorPos(mLastCursorPosOfScreen.x, mLastCursorPosOfScreen.y);
    ShowCursor(true);
}

void App::CalculateFrameStats()
{
// Code computes the average frames per second, and also the 
// average time it takes to render one frame.  These stats 
// are appended to the window caption bar.

    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    frameCnt++;

    // Compute averages over one second period.
    if ((mTimer.TotalTime() - timeElapsed) >= 1.0f)
    {
        float fps = (float)frameCnt; // fps = frameCnt / 1
        float mspf = 1000.0f / fps;

        std::wstring fpsStr = std::to_wstring(fps);
        std::wstring mspfStr = std::to_wstring(mspf);

        std::wstring windowText = mMainWndCaption +
            L"    fps: " + fpsStr +
            L"   mspf: " + mspfStr;

        SetWindowText(mhMainWnd, windowText.c_str());

        // Reset for next average.
        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}

void App::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE
    };
    md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)) >> chk;

    md3dDevice->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&mDirectCmdListAlloc)) >> chk;

    md3dDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        mDirectCmdListAlloc.Get(), // Associated command allocator
        nullptr,                   // Initial PipelineStateObject
        IID_PPV_ARGS(&mCommandList)) >> chk;

    // Start off in a closed state.  This is because the first time we refer 
    // to the command list we will Reset it, and it needs to be closed before
    // calling Reset.
    mCommandList->Close();
}

void App::CreateSwapChain()
{
    // Release the previous swapchain we will be recreating.
    mSwapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd = {
    .BufferDesc = {
        .Width = (UINT)mClientWidth,
        .Height = (UINT)mClientHeight,
        .RefreshRate = {
            .Numerator = 60,
            .Denominator = 1,
        },
        .Format = mBackBufferFormat,
        .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
        .Scaling = DXGI_MODE_SCALING_UNSPECIFIED,
    },
    .SampleDesc = {
        .Count = 1,
        .Quality = 0,
    },
    .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
    .BufferCount = SwapChainBufferCount,
    .OutputWindow = mhMainWnd,
    .Windowed = true,
    .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
    .Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH,
    };

    // Note: Swap chain uses queue to perform flush.
    mdxgiFactory->CreateSwapChain(
        mCommandQueue.Get(),
        &sd,
        &mSwapChain) >> chk;
} 

void App::CreateRtvAndDsvDescriptorHeaps()
{
    const D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = SwapChainBufferCount,   
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };

    md3dDevice->CreateDescriptorHeap(
        &rtvHeapDesc, IID_PPV_ARGS(&mRtvHeap)) >> chk;

    const D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
        .NumDescriptors = 1,   
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0,
    };

    md3dDevice->CreateDescriptorHeap(
        &dsvHeapDesc, IID_PPV_ARGS(&mDsvHeap)) >> chk;
}

void App::FlushCommandQueue()
{
    // Advance the fence value to mark commands up to this fence point.
    mCurrentFence++;

    // Add an instruction to the command queue to set a new fence point.  Because we 
    // are on the GPU timeline, the new fence point won't be set until the GPU finishes
    // processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence) >> chk;

    // Wait until the GPU has completed commands up to this fence point.
    if (mFence->GetCompletedValue() < mCurrentFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        assert(eventHandle);

        // Fire event when GPU hits current fence.  
        mFence->SetEventOnCompletion(mCurrentFence, eventHandle) >> chk;

        // Wait until the GPU hits current fence event is fired.
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

ID3D12Resource* App::CurrentBackBuffer() const
{
    return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE App::CurrentBackBufferView() const
{
    return CD3DX12_CPU_DESCRIPTOR_HANDLE(
        mRtvHeap->GetCPUDescriptorHandleForHeapStart(),
        mCurrBackBuffer,
        mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE App::DepthStencilView() const
{
    return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

// Windows api needs the non-member WndProc function, if declare as global, 
// member mhMainWnd cannot be required. If declare as static member function,
// though mhMainWnd can be required, mhMainWnd should also be static. So 
// calling the member function in a global function.
LRESULT CALLBACK
WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    return App::Get()->MsgProc(hWnd, msg, wParam, lParam);
}
