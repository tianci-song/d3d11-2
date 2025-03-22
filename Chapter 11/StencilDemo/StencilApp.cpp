#include "framework/App.h"
#include "framework/FrameResource.h"
#include "framework/GeometryGenerator.h"
#include "framework/DDSTextureLoader.h"

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
    RenderItem() = default;

    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT ObjCBIndex = -1;

    // The geometry to be drawn. Note, one geometry may need multiple render items.
    MeshGeometry* Geo = nullptr;
    // Note, multiple render items can share one material.
    Material* Mat = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    UINT BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
    Opaque = 0,
    Transparent = 1,
    AlphaTested = 2,
    ReflectedStencil = 3,
    MarkStencil = 4,
    Shadow = 5,
    Count
};

class StencilApp : public App {
public:
    StencilApp(HINSTANCE hInstanceHandle);
    StencilApp(const StencilApp&) = delete;
    StencilApp& operator=(const StencilApp&) = delete;
    ~StencilApp() {}

    virtual bool Initialize() override;

private:
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void OnResize() override;

    void LoadTexture();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildSkullGeometry();
    void BuildRoomGeometry();
    void BuildMaterials();
    void BuildRenderItems();
    void BuildFrameResources();
    void BuildPSOs();

    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateReflectedPassCB(const GameTimer& gt);
    
    // Once the data changed by input, notify the GPU.
    void OnKeyboardInput(const GameTimer& gt);

    void DrawRenderItems(const std::vector<RenderItem*>& ritems);

    float GetHillsHeight(float x, float z) const;
    XMFLOAT3 GetHillsNormal(float x, float z) const;

    std::array<const D3D12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    GeometryGenerator::MeshData LoadModel(const char* filename);

private:
    ComPtr<ID3D12RootSignature> mRootSignature;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;   

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    // List of all the render items.
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;

    // Render items divided by PSO.
    std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

    // Pack the data to be transfered to the GPU constant buffer.
    PassConstants mMainPassCB;
    PassConstants mReflectedPassCB;
    
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    bool mIsWireFrame = false;

    RenderItem* mSkullRitem = nullptr;
    RenderItem* mReflectedSkullRitem = nullptr;
    RenderItem* mShadowedSkullRitem = nullptr;
    XMFLOAT3 mSkullTranslation{ 1.f, 0.f, -5.f };
};

StencilApp::StencilApp(HINSTANCE hInstanceHandle) :
    App(hInstanceHandle)
{
}

bool StencilApp::Initialize()
{
    if (!App::Initialize()) { return false; }

    // Reset the command list to prep for initialization commands.
    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr) >> chk;

    // Create resources: desc heaps, cbv, root signature, etc.
    LoadTexture();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildRoomGeometry();
    BuildSkullGeometry();
    BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    mCommandList->Close() >> chk;
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void StencilApp::Update(const GameTimer& gt)
{
    App::Update(gt);

    OnKeyboardInput(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if (mCurrFrameResource->Fence != 0 &&
        mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle);
        if (eventHandle)
        {
            WaitForSingleObject(eventHandle, INFINITE);
            CloseHandle(eventHandle);
        }
        else
        {
            DWORD errorCode = GetLastError();
            LPVOID lpMsgBuf = 0;
            FormatMessage(
                FORMAT_MESSAGE_ALLOCATE_BUFFER |
                FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPTSTR)&lpMsgBuf,
                0, NULL);
            throw std::runtime_error((char*)lpMsgBuf);
        }
    }

    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
    UpdateReflectedPassCB(gt);
    UpdateMaterialCBs(gt);
}

void StencilApp::Draw(const GameTimer& gt)
{
    auto& cmdListAlloc = mCurrFrameResource->CmdListAlloc;
    cmdListAlloc->Reset() >> chk;

    if (mIsWireFrame)
    {
        mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaqueWireframe"].Get()) >> chk;
    }
    else
    {
        mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()) >> chk;
    }

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(),
        (float*)&mMainPassCB.FogColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Set descriptor heaps on command list.
    ID3D12DescriptorHeap* descHeaps[] = { mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCBByteSize = d3dUtil::CalculateConstantBufferByteSize(sizeof(PassConstants));
    auto passCB = mCurrFrameResource->PassCB->Resource();

    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    //
    // Rendering opaque objects first.
    //

    DrawRenderItems(mRitemLayer[(int)RenderLayer::Opaque]);

    //
    // Marking stencil area. Rendering into stencil buffer, not back buffer.
    //

    mCommandList->OMSetStencilRef(1);
    mCommandList->SetPipelineState(mPSOs["markStencil"].Get());
    DrawRenderItems(mRitemLayer[(int)RenderLayer::MarkStencil]);

    //
    // Drawing reflected objects in stencil area. Need to switch pass to reflectedPass first.
    //

    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress() + (UINT64)1 * passCBByteSize);
    mCommandList->SetPipelineState(mPSOs["reflectedStencil"].Get());
    DrawRenderItems(mRitemLayer[(int)RenderLayer::ReflectedStencil]);

    //
    // Rendering transparent objects after opaque objects. Need to switch pass back to mainPass.
    //

    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());
    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mRitemLayer[(int)RenderLayer::Transparent]);

    //
    // Rendering shadow
    //

    mCommandList->OMSetStencilRef(0);
    mCommandList->SetPipelineState(mPSOs["shadow"].Get());
    DrawRenderItems(mRitemLayer[(int)RenderLayer::Shadow]);

    // Indicate a state transition on the resource usage.
    mCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    mCommandList->Close() >> chk;

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // swap the back and front buffers
    mSwapChain->Present(0, 0) >> chk;
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence) >> chk;
}

void StencilApp::OnResize()
{
    App::OnResize();
}

void StencilApp::LoadTexture()
{
    auto checkboardTex = std::make_unique<Texture>();
    checkboardTex->Name = "checkboardTex";
    checkboardTex->Filename = L"textures/checkboard.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        checkboardTex->Filename.c_str(),
        checkboardTex->Resource,
        checkboardTex->UploadHeap) >> chk;

    auto bricksTex = std::make_unique<Texture>();
    bricksTex->Name = "bricksTex";
    bricksTex->Filename = L"textures/bricks3.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        bricksTex->Filename.c_str(),
        bricksTex->Resource,
        bricksTex->UploadHeap) >> chk;

    auto iceTex = std::make_unique<Texture>();
    iceTex->Name = "iceTex";
    iceTex->Filename = L"textures/ice.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        iceTex->Filename.c_str(),
        iceTex->Resource,
        iceTex->UploadHeap) >> chk;

    auto white1x1Tex = std::make_unique<Texture>();
    white1x1Tex->Name = "white1x1Tex";
    white1x1Tex->Filename = L"textures/white1x1.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        white1x1Tex->Filename.c_str(),
        white1x1Tex->Resource,
        white1x1Tex->UploadHeap) >> chk;

    mTextures[checkboardTex->Name] = std::move(checkboardTex);
    mTextures[bricksTex->Name] = std::move(bricksTex);
    mTextures[iceTex->Name] = std::move(iceTex);
    mTextures[white1x1Tex->Name] = std::move(white1x1Tex);
}

void StencilApp::BuildRootSignature()
{
    // Shader programs typically require resources as input (constant buffers,
    // textures, samplers).  The root signature defines the resources the shader
    // programs expect.  If we think of the shader programs as a function, and
    // the input resources as function parameters, then the root signature can be
    // thought of as defining the function signature.  

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParamter[4];

    //
    // Create root signature.
    //
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    slotRootParamter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    slotRootParamter[1].InitAsConstantBufferView(0);    // ObjectCB
    slotRootParamter[2].InitAsConstantBufferView(1);    // PassCB
    slotRootParamter[3].InitAsConstantBufferView(2);    // MatCB

    auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
    const D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {
        .NumParameters = 4,
        .pParameters = slotRootParamter,
        .NumStaticSamplers = (UINT)staticSamplers.size(),
        .pStaticSamplers = staticSamplers.data(),
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    };

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;

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

void StencilApp::BuildDescriptorHeaps()
{
    //
    // Create the SRV heap.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 4;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)) >> chk;

    //
    // Fill out the heap with actual descriptors.
    //   
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    const auto& checkboardTex = mTextures["checkboardTex"]->Resource;
    const auto& bricksTex = mTextures["bricksTex"]->Resource;
    const auto& iceTex = mTextures["iceTex"]->Resource;
    const auto& white1x1Tex = mTextures["white1x1Tex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    srvDesc.Format = checkboardTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = checkboardTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(checkboardTex.Get(), &srvDesc, hDescriptor);

    srvDesc.Format = bricksTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);    // next descriptor
    md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

    srvDesc.Format = iceTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = iceTex->GetDesc().MipLevels;
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);    // next descriptor
    md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

    srvDesc.Format = white1x1Tex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = white1x1Tex->GetDesc().MipLevels;
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);    // next descriptor
    md3dDevice->CreateShaderResourceView(white1x1Tex.Get(), &srvDesc, hDescriptor);
}

void StencilApp::BuildShadersAndInputLayout()
{
    mInputLayout =
    {
        {   "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   },
        {   "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   },
        {   "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   },
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(L"shader/Default.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["standardPS"] = d3dUtil::CompileShader(L"shader/Default.hlsl", nullptr, "PS", "ps_5_0");
}

void StencilApp::BuildRoomGeometry()
{
// Create and specify geometry.  For this sample we draw a floor
// and a wall with a mirror on it.  We put the floor, wall, and
// mirror geometry in one vertex buffer.
//
//   |--------------|
//   |              |
//   |----|----|----|
//   |Wall|Mirr|Wall|
//   |    | or |    |
//   /--------------/
//  /   Floor      /
// /--------------/

    std::array<Vertex, 20> vertices =
    {
        // Floor: Observe we tile texture coordinates.
        Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 5.0f), // 0 
        Vertex(-3.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
        Vertex(7.5f, 0.0f,   0.0f, 0.0f, 1.0f, 0.0f, 5.5f, 0.0f),
        Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 5.5f, 5.0f),

        // Wall: Observe we tile texture coordinates, and that we
        // leave a gap in the middle for the mirror.
        Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
        Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8 
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.5f, 0.0f),
        Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.5f, 2.0f),

        Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
        Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 5.5f, 0.0f),
        Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 5.5f, 1.0f),

        // Mirror
        Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
        Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
        Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
        Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)
    };

    std::array<std::int16_t, 30> indices =
    {
        // Floor
        0, 1, 2,
        0, 2, 3,

        // Walls
        4, 5, 6,
        4, 6, 7,

        8, 9, 10,
        8, 10, 11,

        12, 13, 14,
        12, 14, 15,

        // Mirror
        16, 17, 18,
        16, 18, 19
    };

    SubmeshGeometry floorSubmesh;
    floorSubmesh.IndexCount = 6;
    floorSubmesh.StartIndexLocation = 0;
    floorSubmesh.BaseVertexLocation = 0;

    SubmeshGeometry wallSubmesh;
    wallSubmesh.IndexCount = 18;
    wallSubmesh.StartIndexLocation = 6;
    wallSubmesh.BaseVertexLocation = 0;

    SubmeshGeometry mirrorSubmesh;
    mirrorSubmesh.IndexCount = 6;
    mirrorSubmesh.StartIndexLocation = 24;
    mirrorSubmesh.BaseVertexLocation = 0;

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "roomGeo";

    D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU) >> chk;
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU);
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["floor"] = floorSubmesh;
    geo->DrawArgs["wall"] = wallSubmesh;
    geo->DrawArgs["mirror"] = mirrorSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void StencilApp::BuildSkullGeometry()
{
    GeometryGenerator::MeshData skull = LoadModel("models/skull.txt");

    std::vector<Vertex> vertices(skull.Vertices.size());
    for (size_t i = 0; i < skull.Vertices.size(); ++i)
    {
        vertices[i].Pos = skull.Vertices[i].Position;
        vertices[i].Normal = skull.Vertices[i].Normal;
    }

    UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = skull.GetIndices16();
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU) >> chk;
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU) >> chk;
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry skullSubmesh;
    skullSubmesh.IndexCount = (UINT)indices.size();
    skullSubmesh.StartIndexLocation = 0;
    skullSubmesh.BaseVertexLocation = 0;

    geo->DrawArgs["skull"] = skullSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void StencilApp::BuildMaterials()
{
    auto checkboardMat = std::make_unique<Material>();
    checkboardMat->Name = "checkboardMat";
    checkboardMat->MatCBIndex = 0;
    checkboardMat->DiffuseSrvHeapIndex = 0;
    checkboardMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkboardMat->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
    checkboardMat->Roughness = 0.3f;

    auto bricksMat = std::make_unique<Material>();
    bricksMat->Name = "bricksMat";
    bricksMat->MatCBIndex = 1;
    bricksMat->DiffuseSrvHeapIndex = 1;
    bricksMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricksMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    bricksMat->Roughness = 0.25f;

    auto iceMat = std::make_unique<Material>();
    iceMat->Name = "iceMat";
    iceMat->MatCBIndex = 2;
    iceMat->DiffuseSrvHeapIndex = 2;
    iceMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
    iceMat->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    iceMat->Roughness = 0.5f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 3;
    skullMat->DiffuseSrvHeapIndex = 3;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;

    auto shadowMat = std::make_unique<Material>();
    shadowMat->Name = "shadowMat";
    shadowMat->MatCBIndex = 4;
    shadowMat->DiffuseSrvHeapIndex = 3;
    shadowMat->DiffuseAlbedo = XMFLOAT4(0.f, 0.f, 0.f, 0.5f);
    shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
    shadowMat->Roughness = 0.f;

    mMaterials[checkboardMat->Name] = std::move(checkboardMat);
    mMaterials[bricksMat->Name] = std::move(bricksMat);
    mMaterials[iceMat->Name] = std::move(iceMat);
    mMaterials[skullMat->Name] = std::move(skullMat);
    mMaterials[shadowMat->Name] = std::move(shadowMat);
}

void StencilApp::BuildRenderItems()
{
    auto floorRitem = std::make_unique<RenderItem>();
    floorRitem->World = MathHelper::Identity4x4();
    floorRitem->TexTransform = MathHelper::Identity4x4();
    floorRitem->ObjCBIndex = 0;
    floorRitem->Geo = mGeometries["roomGeo"].get();
    floorRitem->Mat = mMaterials["checkboardMat"].get();
    floorRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry floorSubmesh = floorRitem->Geo->DrawArgs["floor"];
    floorRitem->IndexCount = floorSubmesh.IndexCount;
    floorRitem->StartIndexLocation = floorSubmesh.StartIndexLocation;
    floorRitem->BaseVertexLocation = floorSubmesh.BaseVertexLocation;

    auto wallRitem = std::make_unique<RenderItem>();
    wallRitem->World = MathHelper::Identity4x4();
    wallRitem->TexTransform = MathHelper::Identity4x4();
    wallRitem->ObjCBIndex = 1;
    wallRitem->Geo = mGeometries["roomGeo"].get();
    wallRitem->Mat = mMaterials["bricksMat"].get();
    wallRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry wallSubmesh = wallRitem->Geo->DrawArgs["wall"];
    wallRitem->IndexCount = wallSubmesh.IndexCount;
    wallRitem->StartIndexLocation = wallSubmesh.StartIndexLocation;
    wallRitem->BaseVertexLocation = wallSubmesh.BaseVertexLocation;

    auto mirrorRitem = std::make_unique<RenderItem>();
    mirrorRitem->World = MathHelper::Identity4x4();
    mirrorRitem->TexTransform = MathHelper::Identity4x4();
    mirrorRitem->ObjCBIndex = 2;
    mirrorRitem->Geo = mGeometries["roomGeo"].get();
    mirrorRitem->Mat = mMaterials["iceMat"].get();
    mirrorRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry mirrorSubmesh = mirrorRitem->Geo->DrawArgs["mirror"];
    mirrorRitem->IndexCount = mirrorSubmesh.IndexCount;
    mirrorRitem->StartIndexLocation = mirrorSubmesh.StartIndexLocation;
    mirrorRitem->BaseVertexLocation = mirrorSubmesh.BaseVertexLocation;

    auto skullRitem = std::make_unique<RenderItem>();
    skullRitem->World = MathHelper::Identity4x4();
    skullRitem->TexTransform = MathHelper::Identity4x4();
    skullRitem->ObjCBIndex = 3;
    skullRitem->Geo = mGeometries["skullGeo"].get();
    skullRitem->Mat = mMaterials["skullMat"].get();
    skullRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry skullSubmesh = skullRitem->Geo->DrawArgs["skull"];
    skullRitem->IndexCount = skullSubmesh.IndexCount;
    skullRitem->StartIndexLocation = skullSubmesh.StartIndexLocation;
    skullRitem->BaseVertexLocation = skullSubmesh.BaseVertexLocation;
    mSkullRitem = skullRitem.get();

    auto reflectedSkullRitem = std::make_unique<RenderItem>();
    *reflectedSkullRitem = *skullRitem;
    reflectedSkullRitem->ObjCBIndex = 4;
    mReflectedSkullRitem = reflectedSkullRitem.get();

    auto shadowedSkullRitem = std::make_unique<RenderItem>();
    *shadowedSkullRitem = *skullRitem;
    shadowedSkullRitem->ObjCBIndex = 5;
    shadowedSkullRitem->Mat = mMaterials["shadowMat"].get();
    mShadowedSkullRitem = shadowedSkullRitem.get();

    //
    // exercise 11: reflecting floor
    //

    auto reflectedFloorRitem = std::make_unique<RenderItem>();
    *reflectedFloorRitem = *floorRitem;
    reflectedFloorRitem->ObjCBIndex = 6;
    XMVECTOR mirrorPlane = XMVectorSet(0.f, 0.f, 1.f, 0.f);   // Ax+By+Cz+D=0, here z=0
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&reflectedFloorRitem->World, R);


    mRitemLayer[(int)RenderLayer::Opaque].push_back(floorRitem.get());
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wallRitem.get());
    mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());

    mRitemLayer[(int)RenderLayer::MarkStencil].push_back(mirrorRitem.get());

    mRitemLayer[(int)RenderLayer::ReflectedStencil].push_back(reflectedSkullRitem.get());
    mRitemLayer[(int)RenderLayer::ReflectedStencil].push_back(reflectedFloorRitem.get());

    mRitemLayer[(int)RenderLayer::Transparent].push_back(mirrorRitem.get());

    mRitemLayer[(int)RenderLayer::Shadow].push_back(shadowedSkullRitem.get());

    mAllRitems.push_back(std::move(floorRitem));
    mAllRitems.push_back(std::move(wallRitem));
    mAllRitems.push_back(std::move(mirrorRitem));
    mAllRitems.push_back(std::move(skullRitem));
    mAllRitems.push_back(std::move(reflectedSkullRitem));
    mAllRitems.push_back(std::move(reflectedFloorRitem));
    mAllRitems.push_back(std::move(shadowedSkullRitem));
}

void StencilApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 2, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void StencilApp::BuildPSOs()
{
    //
    // PSO for opaque objects.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = {
        .pRootSignature = mRootSignature.Get(),
        .VS = {
            reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
            mShaders["standardVS"]->GetBufferSize() },
        .PS = {
            reinterpret_cast<BYTE*>(mShaders["standardPS"]->GetBufferPointer()),
            mShaders["standardPS"]->GetBufferSize() },
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
            .Quality = m4xMsaaState ? m4xMsaaQuality - 1 : 0u },
    };

    md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc,
        IID_PPV_ARGS(&mPSOs["opaque"])) >> chk;

    //
    // PSO for marking the stencil, for mirror reflection.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC markStencilPsoDesc = opaquePsoDesc;

    // Disable writing into back buffer.
    markStencilPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;

    markStencilPsoDesc.DepthStencilState = {
        .DepthEnable = true,
        .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,  // depth test enable, depth write disable
        .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
        .StencilEnable = true,
        .StencilReadMask = 0xff,
        .StencilWriteMask = 0xff,
        .FrontFace = {
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_REPLACE,
            .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        },
        // No need to care about these settings since we don't render the back face.
        .BackFace = {
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_REPLACE,
            .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        },
    };

    md3dDevice->CreateGraphicsPipelineState(&markStencilPsoDesc,
        IID_PPV_ARGS(&mPSOs["markStencil"])) >> chk;

    //
    // PSO for rendering the reflected objects in stencil-marked area (mirror).
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC reflectedStencilPsoDesc = opaquePsoDesc;

    reflectedStencilPsoDesc.DepthStencilState = {
        .DepthEnable = true,
        .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
        .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
        .StencilEnable = true,
        .StencilReadMask = 0xff,
        .StencilWriteMask = 0xff,
        .FrontFace = {
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_KEEP,
            .StencilFunc = D3D12_COMPARISON_FUNC_EQUAL
        },
        // No need to care about these settings since we don't render the back face.
        .BackFace = {
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_REPLACE,
            .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS
        }
    };

    reflectedStencilPsoDesc.RasterizerState.FrontCounterClockwise = true;

    md3dDevice->CreateGraphicsPipelineState(&reflectedStencilPsoDesc,
        IID_PPV_ARGS(&mPSOs["reflectedStencil"])) >> chk;

    //
    // Pso for transparent objects
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;
    D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc = {
        .BlendEnable = true,
        .LogicOpEnable = false,
        .SrcBlend = D3D12_BLEND_SRC_ALPHA,
        .DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
        .BlendOp = D3D12_BLEND_OP_ADD,
        .SrcBlendAlpha = D3D12_BLEND_ONE,
        .DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA,
        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
        .LogicOp = D3D12_LOGIC_OP_NOOP,
        .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
    };
    transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;

    md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, 
        IID_PPV_ARGS(&mPSOs["transparent"])) >> chk;

    //
    // PSO for shadow
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = transparentPsoDesc;
    shadowPsoDesc.DepthStencilState = {
        .DepthEnable = true,
        .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
        .DepthFunc = D3D12_COMPARISON_FUNC_LESS,
        .StencilEnable = true,
        .StencilReadMask = 0xff,
        .StencilWriteMask = 0xff,
        .FrontFace = {
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_INCR,
            .StencilFunc = D3D12_COMPARISON_FUNC_EQUAL
        },
        // No need to care about these settings since we don't render the back face.
        .BackFace = {
            .StencilFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
            .StencilPassOp = D3D12_STENCIL_OP_INCR,
            .StencilFunc = D3D12_COMPARISON_FUNC_EQUAL
        }
    };

    md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc,
        IID_PPV_ARGS(&mPSOs["shadow"])) >> chk;

    //
    // PSO for opaque wireframe objects.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

    md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc,
        IID_PPV_ARGS(&mPSOs["opaqueWireframe"])) >> chk;
}

void StencilApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    for (auto& ri : mAllRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (ri->NumFramesDirty > 0)
        {
            ObjectConstants objConstants;
            XMMATRIX world = XMLoadFloat4x4(&ri->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&ri->TexTransform);
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(ri->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            ri->NumFramesDirty--;
        }
    }
}

void StencilApp::UpdateMaterialCBs(const GameTimer& gt)
{
    auto currMaterialCB = mCurrFrameResource->MaterialCB.get();

    // Only update the cbuffer data if the constants have changed.  If the cbuffer
    // data changes, it needs to be updated for each FrameResource.
    for (auto& e : mMaterials)
    {
        Material* mat = e.second.get();
        if (mat->NumFramesDirty > 0)
        {
            MaterialConstants matConstants;
            matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
            matConstants.FresnelR0 = mat->FresnelR0;
            matConstants.Roughness = mat->Roughness;

            XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);
            XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

            currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

            // Next FrameResource need to be updated too.
            mat->NumFramesDirty--;
        }   
    }
}

void StencilApp::UpdateMainPassCB(const GameTimer& gt)
{
    XMMATRIX viewProj = XMMatrixMultiply(mView, mProj);
    XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(mView), mView);
    XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(mProj), mProj);
    XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);
    XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(mView));
    XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(mProj));
    XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
    XMStoreFloat3(&mMainPassCB.EyePosW, mCameraPos);
    mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.f / mClientWidth, 1.f / mClientHeight);
    mMainPassCB.NearZ = 1.f;
    mMainPassCB.FarZ = 1000.f;
    mMainPassCB.TotalTime = gt.TotalTime();
    mMainPassCB.DeltaTime = gt.DeltaTime();
    mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.9f, 0.9f, 0.9f };
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void StencilApp::UpdateReflectedPassCB(const GameTimer& gt)
{
    mReflectedPassCB = mMainPassCB;

    XMVECTOR mirrorPlane = XMVectorSet(0.f, 0.f, 1.f, 0.f);
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    for (int i = 0; i < 3; ++i)
    {
        XMVECTOR lightDir = XMLoadFloat3(&mMainPassCB.Lights[i].Direction);
        XMVECTOR reflectedLightDir = XMVector3TransformNormal(lightDir, R);
        XMStoreFloat3(&mReflectedPassCB.Lights[i].Direction, reflectedLightDir);
    }

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(1, mReflectedPassCB);
}

void StencilApp::OnKeyboardInput(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        mSkullTranslation.z -= 1.0f * dt;

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        mSkullTranslation.z += 1.0f * dt;

    if (GetAsyncKeyState(VK_UP) & 0x8000)
        mSkullTranslation.x -= 1.0f * dt;

    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        mSkullTranslation.x += 1.0f * dt;

    // Update the new world matrix.
    XMMATRIX skullRotate = XMMatrixRotationY(XM_PIDIV2);
    XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
    XMMATRIX skullOffset = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
    XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
    XMStoreFloat4x4(&mSkullRitem->World, skullWorld);

    XMVECTOR mirrorPlane = XMVectorSet(0.f, 0.f, 1.f, 0.f);   // Ax+By+Cz+D=0, here z=0
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&mReflectedSkullRitem->World, skullWorld * R);

    XMVECTOR shadowPlane = XMVectorSet(0.f, 1.f, 0.f, 0.f);
    XMVECTOR toMainLight = -XMLoadFloat3(&mMainPassCB.Lights[0].Direction);
    XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.f, 0.001f, 0.f);
    XMStoreFloat4x4(&mShadowedSkullRitem->World, skullWorld * S * shadowOffsetY);

    mSkullRitem->NumFramesDirty = gNumFrameResources;
    mReflectedSkullRitem->NumFramesDirty = gNumFrameResources;
    mShadowedSkullRitem->NumFramesDirty = gNumFrameResources;
}

void StencilApp::DrawRenderItems(const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalculateConstantBufferByteSize(
        sizeof(ObjectConstants));   
    UINT matCBByteSize = d3dUtil::CalculateConstantBufferByteSize(
        sizeof(MaterialConstants));

    auto objectCB = mCurrFrameResource->ObjectCB->Resource();
    auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for (const RenderItem* ri: ritems)
    {
        mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

        CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvHeap->GetGPUDescriptorHandleForHeapStart());
        tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = 
            objectCB->GetGPUVirtualAddress() + (UINT64)ri->ObjCBIndex * objCBByteSize;
        D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = 
            matCB->GetGPUVirtualAddress() + (UINT64)ri->Mat->MatCBIndex * matCBByteSize;

        mCommandList->SetGraphicsRootDescriptorTable(0, tex);
        mCommandList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        mCommandList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, 
            ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

float StencilApp::GetHillsHeight(float x, float z) const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 StencilApp::GetHillsNormal(float x, float z) const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
        1.0f,
        -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}

std::array<const D3D12_STATIC_SAMPLER_DESC, 6> StencilApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp };
}

GeometryGenerator::MeshData StencilApp::LoadModel(const char* filename)
{
    GeometryGenerator::MeshData meshData;
    int vertexCount = 0;
    int indexCount = 0;

    std::ifstream in(filename);
    std::string str;
    char ch[100];

    in.getline(ch, 100);
    str = ch;
    if (str.substr(0, 13) == "VertexCount: ")
    {
        vertexCount = std::atol(str.substr(13, str.size() - 13).c_str());
    }

    in.getline(ch, 100);
    str = ch;
    if (str.substr(0, 15) == "TriangleCount: ")
    {
        indexCount = 3 * std::atol(str.substr(15, str.size() - 15).c_str());
    }

    in.getline(ch, 100);    // flush
    in.getline(ch, 100);    // flush

    // Extract vertex data
    in.getline(ch, 100);
    str = ch;
    do {
        str.erase(str.cbegin(), std::find_if(str.cbegin(), str.cend(), [](char ch) { return ch != '\t'; }));

        float vData[6];
        for (int i = 0; i < 6; ++i)
        {
            vData[i] = (float)std::atof(str.substr(0, str.find(' ')).c_str());
            str = str.substr(str.find(' ') + 1, str.size());
        }
        GeometryGenerator::Vertex v;
        v.Position = XMFLOAT3(vData[0], vData[1], vData[2]);
        v.Normal = XMFLOAT3(vData[3], vData[4], vData[5]);
        meshData.Vertices.push_back(v);

        in.getline(ch, 100);
        str = ch;
    } while (str != "}");

    assert(meshData.Vertices.size() == vertexCount);

    in.getline(ch, 100);    // flush
    in.getline(ch, 100);    // flush

    // Extract index data
    in.getline(ch, 100);
    str = ch;
    do {
        str.erase(str.cbegin(), std::find_if(str.cbegin(), str.cend(), [](char ch) { return ch != '\t'; }));

        for (int i = 0; i < 3; ++i)
        {
            int index = std::atoi(str.substr(0, str.find(' ')).c_str());
            str = str.substr(str.find(' ') + 1, str.size());
            meshData.Indices32.push_back(index);
        }
        in.getline(ch, 100);
        str = ch;
    } while (str != "}");

    assert(meshData.Indices32.size() == indexCount);

    return meshData;
}

int WINAPI
WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ PSTR pCmdLine, _In_ int nCmdShow)
{
    try {
        StencilApp app(hInstance);
        if (!app.Initialize()) { return 0; }
        return app.Run();
    }
    catch (std::exception e) {
        MessageBoxA(nullptr, e.what(), "Graphics Error", MB_OK);
        return 0;
    }
}