#include "framework/App.h"
#include "framework/UploadBuffer.h"

struct Vertex {
    dx::XMFLOAT3 Pos;
    dx::XMFLOAT4 Color;
};

class BoxApp : public App {
public:
    BoxApp(HINSTANCE hInstanceHandle);
    BoxApp(const BoxApp&) = delete;
    BoxApp& operator=(const BoxApp&) = delete;
    ~BoxApp() {}

    virtual bool Initialize() override;

private:
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void OnResize() override;

    void BuildDescriptorHeaps();
    void BuildConstantBuffers();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildBoxGeometry();
    void BuildPSO();

private:
    wrl::ComPtr<ID3D12RootSignature> mRootSignature;

    struct ObjectConstants {
        dx::XMFLOAT4X4 WorldViewProj = MathHelper::Identity4x4();
    };
    std::unique_ptr< UploadBuffer<ObjectConstants> > mObjectCB;

    wrl::ComPtr<ID3DBlob> mVsByteCode;
    wrl::ComPtr<ID3DBlob> mPsByteCode;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::unique_ptr<MeshGeometry> mBoxGeo;

    wrl::ComPtr<ID3D12PipelineState> mPSO;
};

BoxApp::BoxApp(HINSTANCE hInstanceHandle) :
    App(hInstanceHandle)
{
}

bool BoxApp::Initialize()
{
    if (!App::Initialize()) { return false; }

    // Reset the command list to prep for initialization commands.
    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr) >> chk;

    // Create resources: desc heaps, cbv, root signature, etc.
    BuildDescriptorHeaps();
    BuildConstantBuffers();
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildBoxGeometry();
    BuildPSO();

    // Execute the initialization commands.
    mCommandList->Close() >> chk;
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void BoxApp::Update(const GameTimer& gt)
{
    App::Update(gt);

    ObjectConstants objConstants;
    dx::XMStoreFloat4x4(&objConstants.WorldViewProj, dx::XMMatrixTranspose(mWorldViewProj));
    mObjectCB->CopyData(0, objConstants);
}

void BoxApp::Draw(const GameTimer& gt)
{
    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    mDirectCmdListAlloc->Reset() >> chk;

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    mCommandList->Reset(mDirectCmdListAlloc.Get(), mPSO.Get()) >> chk;

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Bind vertex buffer and index buffer to the pipeline.
    mCommandList->IASetVertexBuffers(0u, 1u, &mBoxGeo->VertexBufferView());
    mCommandList->IASetIndexBuffer(&mBoxGeo->IndexBufferView());
    mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind root signature to the pipeline.
    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    // Bind descriptor heaps to the pipeline.
    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    // Bind descriptor table to the pipeline.
    mCommandList->SetGraphicsRootDescriptorTable(0u, mCbvHeap->GetGPUDescriptorHandleForHeapStart());

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    const SubmeshGeometry& submesh = mBoxGeo->DrawArgs["Box"];
    ThrowIfFailed_VOID(mCommandList->DrawIndexedInstanced(
        submesh.IndexCount,
        1u,
        submesh.StartIndexLocation,
        submesh.BaseVertexLocation,
        0u));

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    mCommandList->Close() >> chk;

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // swap the back and front buffers
    mSwapChain->Present(0, 0) >> chk;
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Wait until frame commands are complete.  This waiting is inefficient and is
    // done for simplicity.  Later we will show how to organize our rendering code
    // so we do not have to wait per frame.
    FlushCommandQueue();
}

void BoxApp::OnResize()
{
    App::OnResize();
}

void BoxApp::BuildDescriptorHeaps()
{
    const D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = 1u,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
        .NodeMask = 0u,
    };

    md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap));
}

void BoxApp::BuildConstantBuffers()
{
    mObjectCB = std::make_unique< UploadBuffer<ObjectConstants> >(
        md3dDevice.Get(), 1u, true);

    const UINT cbByteSize = d3dUtil::CalculateConstantBufferByteSize(
        sizeof(ObjectConstants));
    auto cbAddress = mObjectCB->Resource()->GetGPUVirtualAddress();

    // Offset to the ith object constant buffer in the buffer.
    const UINT cbIndex = 0u;
    cbAddress += (UINT64)cbIndex * cbByteSize;

    const D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
        .BufferLocation = cbAddress,
        .SizeInBytes = cbByteSize,
    };

    md3dDevice->CreateConstantBufferView(
        &cbvDesc, 
        mCbvHeap->GetCPUDescriptorHandleForHeapStart());
}

void BoxApp::BuildRootSignature()
{
    // Shader programs typically require resources as input (constant buffers,
    // textures, samplers).  The root signature defines the resources the shader
    // programs expect.  If we think of the shader programs as a function, and
    // the input resources as function parameters, then the root signature can be
    // thought of as defining the function signature.  

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParamter[1];

    // Create a single descriptor table of CBVs.
    CD3DX12_DESCRIPTOR_RANGE cbvTable;
    cbvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1u, 0u);
    slotRootParamter[0].InitAsDescriptorTable(1u, &cbvTable);

    // A root signature is an array of root parameters.
    const D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {
        .NumParameters = 1u,
        .pParameters = slotRootParamter,
        .NumStaticSamplers = 0u,
        .pStaticSamplers = nullptr,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    };

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    wrl::ComPtr<ID3DBlob> serializedRootSig;
    wrl::ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &serializedRootSig,
        &errorBlob);

    if (errorBlob != nullptr) {
        throw std::runtime_error((char*)errorBlob->GetBufferPointer());
    } hr >> chk;

    md3dDevice->CreateRootSignature(
        0u,
        serializedRootSig->GetBufferPointer(), 
        serializedRootSig->GetBufferSize(), 
        IID_PPV_ARGS(&mRootSignature)) >> chk;
}

void BoxApp::BuildShadersAndInputLayout()
{
    mInputLayout = 
    {
        {   "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   },
        {   "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   }
    };

    mVsByteCode = d3dUtil::CompileShader(L"shader/color.hlsl", nullptr, "VS", "vs_5_0");
    mPsByteCode = d3dUtil::CompileShader(L"shader/color.hlsl", nullptr, "PS", "ps_5_0");
}

void BoxApp::BuildBoxGeometry()
{
    Vertex vertices[] = {
        {   dx::XMFLOAT3(-1.f, -1.f, -1.f), dx::XMFLOAT4(dx::Colors::White)     },
        {   dx::XMFLOAT3(-1.f, +1.f, -1.f), dx::XMFLOAT4(dx::Colors::Black)     },
        {   dx::XMFLOAT3(+1.f, +1.f, -1.f), dx::XMFLOAT4(dx::Colors::Red)       },
        {   dx::XMFLOAT3(+1.f, -1.f, -1.f), dx::XMFLOAT4(dx::Colors::Green)     },
        {   dx::XMFLOAT3(-1.f, -1.f, +1.f), dx::XMFLOAT4(dx::Colors::Blue)      },
        {   dx::XMFLOAT3(-1.f, +1.f, +1.f), dx::XMFLOAT4(dx::Colors::Yellow)    },
        {   dx::XMFLOAT3(+1.f, +1.f, +1.f), dx::XMFLOAT4(dx::Colors::Cyan)      },
        {   dx::XMFLOAT3(+1.f, -1.f, +1.f), dx::XMFLOAT4(dx::Colors::Magenta)   }
    };

    const std::uint16_t indices[] = {
        // front face
        0, 1, 2,
        0, 2, 3,

        // back face
        4, 6, 5,
        4, 7, 6,

        // left
        4, 5, 1,
        4, 1, 0,

        // right
        3, 2, 6,
        3, 6, 7,

        // top
        1, 5, 6,
        1, 6, 2,

        // bottom
        4, 0, 3,
        4, 3, 7
    };

    const UINT64 vbByteSize = sizeof(vertices);
    const UINT64 ibByteSize = sizeof(indices);

    mBoxGeo = std::make_unique<MeshGeometry>();
    mBoxGeo->Name = "BoxGeo";

    D3DCreateBlob(vbByteSize, &mBoxGeo->VertexBufferCPU) >> chk;
    CopyMemory(mBoxGeo->VertexBufferCPU->GetBufferPointer(), vertices, vbByteSize);

    D3DCreateBlob(ibByteSize, &mBoxGeo->IndexBufferCPU) >> chk;
    CopyMemory(mBoxGeo->IndexBufferCPU->GetBufferPointer(), indices, ibByteSize);

    mBoxGeo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        vertices,
        vbByteSize,
        mBoxGeo->VertexBufferUploader);

    mBoxGeo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        indices,
        ibByteSize,
        mBoxGeo->IndexBufferUploader);

    mBoxGeo->VertexByteStride = sizeof(Vertex);
    mBoxGeo->VertexBufferByteSize = vbByteSize;
    mBoxGeo->IndexBufferByteSize = ibByteSize;
    mBoxGeo->IndexFormat = DXGI_FORMAT_R16_UINT;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)std::size(indices);
    submesh.StartIndexLocation = 0u;
    submesh.BaseVertexLocation = 0u;

    mBoxGeo->DrawArgs["Box"] = submesh;
}

void BoxApp::BuildPSO()
{
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {
        .pRootSignature = mRootSignature.Get(),
        .VS = {
            reinterpret_cast<BYTE*>(mVsByteCode->GetBufferPointer()), 
            mVsByteCode->GetBufferSize() },
        .PS = {
            reinterpret_cast<BYTE*>(mPsByteCode->GetBufferPointer()), 
            mPsByteCode->GetBufferSize() },
        .BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT),
        .SampleMask = UINT_MAX,
        .RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
        .DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT),
        .InputLayout = { 
            mInputLayout.data(), 
            (UINT)mInputLayout.size()},
        .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
        .NumRenderTargets = 1u,
        .RTVFormats = { mBackBufferFormat },
        .DSVFormat = mDepthStencilFormat,
        .SampleDesc = {
            .Count = m4xMsaaState ? 4u : 1u, 
            .Quality = m4xMsaaState ? m4xMsaaQuality -1 : 0u },
    };

    md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)) >> chk;
}

int WINAPI
WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ PSTR pCmdLine, _In_ int nCmdShow)
{
    try {
        BoxApp app(hInstance);
        if (!app.Initialize()) { return 0; }
        return app.Run();
    }
    catch (std::exception e) {
        MessageBoxA(nullptr, e.what(), "Graphics Error", MB_OK);
        return 0;
    }
}