#include "framework/App.h"
#include "framework/FrameResource.h"
#include "framework/GeometryGenerator.h"
#include "framework/DDSTextureLoader.h"
#include "Waves.h"

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
    Count
};

class TexWavesApp : public App {
public:
    TexWavesApp(HINSTANCE hInstanceHandle);
    TexWavesApp(const TexWavesApp&) = delete;
    TexWavesApp& operator=(const TexWavesApp&) = delete;
    ~TexWavesApp() {}

    virtual bool Initialize() override;

private:
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void OnResize() override;

    void LoadTexture();
    void BuildRootSignature();
    void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildCrateGeometry();
    void BuildLandGeometry();
    void BuildWavesGeometry();
    void BuildMaterials();
    void BuildRenderItems();
    void BuildFrameResources();
    void BuildPSOs();
    
    void AnimateMaterials(const GameTimer& gt);
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateSunPosition(const GameTimer& gt);
    void UpdateWaves(const GameTimer& gt);   

    void DrawRenderItems(const std::vector<RenderItem*>& ritems);

    float GetHillsHeight(float x, float z) const;
    XMFLOAT3 GetHillsNormal(float x, float z) const;

    std::array<const D3D12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

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

    // FrameResource and RenderItem own the same dynamic vertex buffer.
    // Unlike other render items, it is updated each frame, so should be saved as a reference.
    RenderItem* mWavesRitem = nullptr;

    // Pack the data to be transfered to the GPU constant buffer.
    PassConstants mMainPassCB;
    UINT mPassCbvOffset = 0;
    
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    bool mIsWireFrame = false;

    std::unique_ptr<Waves> mWaves;

    float mSunTheta = 1.25f * XM_PI;
    float mSunPhi = XM_PIDIV4;
};

TexWavesApp::TexWavesApp(HINSTANCE hInstanceHandle) :
    App(hInstanceHandle)
{
}

bool TexWavesApp::Initialize()
{
    if (!App::Initialize()) { return false; }

    // Reset the command list to prep for initialization commands.
    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr) >> chk;

    mWaves = std::make_unique<Waves>(128, 128, 1.f, 0.03f, 4.f, 0.2f);

    // Create resources: desc heaps, cbv, root signature, etc.
    LoadTexture();
    BuildRootSignature();
    BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildCrateGeometry();
    BuildLandGeometry();
    BuildWavesGeometry();
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

void TexWavesApp::Update(const GameTimer& gt)
{
    App::Update(gt);

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

    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
    UpdateMaterialCBs(gt);
    UpdateSunPosition(gt);
    UpdateWaves(gt);
}

void TexWavesApp::Draw(const GameTimer& gt)
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
        DirectX::Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    // Set descriptor heaps on command list.
    ID3D12DescriptorHeap* descHeaps[] = { mSrvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descHeaps), descHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCB = mCurrFrameResource->PassCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mRitemLayer[(int)RenderLayer::Opaque]);

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

void TexWavesApp::OnResize()
{
    App::OnResize();
}

void TexWavesApp::LoadTexture()
{
    auto woodCrateTex = std::make_unique<Texture>();
    woodCrateTex->Name = "woodCrateTex";
    woodCrateTex->Filename = L"textures/woodCrate01.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        woodCrateTex->Filename.c_str(),
        woodCrateTex->Resource,
        woodCrateTex->UploadHeap) >> chk;

    auto grassTex = std::make_unique<Texture>();
    grassTex->Name = "grassTex";
    grassTex->Filename = L"textures/grass.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        grassTex->Filename.c_str(),
        grassTex->Resource,
        grassTex->UploadHeap) >> chk;

    auto waterTex = std::make_unique<Texture>();
    waterTex->Name = "waterTex";
    waterTex->Filename = L"textures/water1.dds";
    CreateDDSTextureFromFile12(
        md3dDevice.Get(),
        mCommandList.Get(),
        waterTex->Filename.c_str(),
        waterTex->Resource,
        waterTex->UploadHeap) >> chk;

    mTextures[woodCrateTex->Name] = std::move(woodCrateTex);
    mTextures[grassTex->Name] = std::move(grassTex);
    mTextures[waterTex->Name] = std::move(waterTex);
}

void TexWavesApp::BuildRootSignature()
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

void TexWavesApp::BuildDescriptorHeaps()
{
    //
    // Create the SRV heap.
    //
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 3;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvHeap)) >> chk;

    //
    // Fill out the heap with actual descriptors.
    //   
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        mSrvHeap->GetCPUDescriptorHandleForHeapStart());

    const auto& woodCrateTex = mTextures["woodCrateTex"]->Resource;
    const auto& grassTex = mTextures["grassTex"]->Resource;
    const auto& waterTex = mTextures["waterTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    srvDesc.Format = woodCrateTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = woodCrateTex->GetDesc().MipLevels;
    md3dDevice->CreateShaderResourceView(woodCrateTex.Get(), &srvDesc, hDescriptor);

    srvDesc.Format = grassTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = grassTex->GetDesc().MipLevels;
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);    // next descriptor
    md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

    srvDesc.Format = waterTex->GetDesc().Format;
    srvDesc.Texture2D.MipLevels = waterTex->GetDesc().MipLevels;
    hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);    // next descriptor
    md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);
}

void TexWavesApp::BuildShadersAndInputLayout()
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
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"shader/Default.hlsl", nullptr, "PS", "ps_5_0");
}

void TexWavesApp::BuildCrateGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = 0;
    boxSubmesh.BaseVertexLocation = 0;

    std::vector<Vertex> vertices(box.Vertices.size());

    for (size_t i = 0; i < box.Vertices.size(); ++i)
    {
        vertices[i].Pos = box.Vertices[i].Position;
        vertices[i].Normal = box.Vertices[i].Normal;
        vertices[i].TexC = box.Vertices[i].TexC;
    }

    std::vector<std::uint16_t> indices = box.GetIndices16();

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "boxGeo";

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

    geo->DrawArgs["box"] = boxSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void TexWavesApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        XMFLOAT3 p = grid.Vertices[i].Position;

        vertices[i].Pos = p;
        vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
        vertices[i].Normal = GetHillsNormal(p.x, p.z);
        vertices[i].TexC = grid.Vertices[i].TexC;
    }

    UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = grid.GetIndices16();
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "landGeo";

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

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void TexWavesApp::BuildWavesGeometry()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
    assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for (int i = 0; i < m - 1; ++i)
    {
        for (int j = 0; j < n - 1; ++j)
        {
            indices[k] = i * n + j;
            indices[k + 1] = i * n + j + 1;
            indices[k + 2] = (i + 1) * n + j;

            indices[k + 3] = (i + 1) * n + j;
            indices[k + 4] = i * n + j + 1;
            indices[k + 5] = (i + 1) * n + j + 1;

            k += 6; // next quad
        }
    }

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "wavesGeo";

    // Set dynamically.
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU) >> chk;
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void TexWavesApp::BuildMaterials()
{
    auto woodCrate = std::make_unique<Material>();
    woodCrate->Name = "woodCrate";
    woodCrate->MatCBIndex = 0;
    woodCrate->DiffuseSrvHeapIndex = 0;
    woodCrate->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    woodCrate->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    woodCrate->Roughness = 0.2f;

    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 1;
    grass->DiffuseSrvHeapIndex = 1;
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    grass->Roughness = 0.125f;

    // This is not a good water material definition, but we do not have all the rendering
    // tools we need (transparency, environment reflection), so we fake it for now.
    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = 2;
    water->DiffuseSrvHeapIndex = 2;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    water->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    water->Roughness = 0.f;

    mMaterials[woodCrate->Name] = std::move(woodCrate);
    mMaterials[grass->Name] = std::move(grass);
    mMaterials[water->Name] = std::move(water);
}

void TexWavesApp::BuildRenderItems()
{
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
    boxRitem->ObjCBIndex = 0;
    boxRitem->Mat = mMaterials["woodCrate"].get();
    boxRitem->Geo = mGeometries["boxGeo"].get();
    boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry boxSubmesh = boxRitem->Geo->DrawArgs["box"];
    boxRitem->IndexCount = boxSubmesh.IndexCount;
    boxRitem->StartIndexLocation = boxSubmesh.StartIndexLocation;
    boxRitem->BaseVertexLocation = boxSubmesh.BaseVertexLocation;

    auto landRitem = std::make_unique<RenderItem>();
    landRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&landRitem->TexTransform, XMMatrixScaling(5.f, 5.f, 1.f));
    landRitem->ObjCBIndex = 1;
    landRitem->Geo = mGeometries["landGeo"].get();
    landRitem->Mat = mMaterials["grass"].get();
    landRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry landSubmesh = landRitem->Geo->DrawArgs["grid"];
    landRitem->IndexCount = landSubmesh.IndexCount;
    landRitem->StartIndexLocation = landSubmesh.StartIndexLocation;
    landRitem->BaseVertexLocation = landSubmesh.BaseVertexLocation;

    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
    XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.f, 5.f, 1.f));
    wavesRitem->ObjCBIndex = 2;
    wavesRitem->Geo = mGeometries["wavesGeo"].get();
    wavesRitem->Mat = mMaterials["water"].get();
    wavesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry wavesSubmesh = wavesRitem->Geo->DrawArgs["grid"];
    wavesRitem->IndexCount = wavesSubmesh.IndexCount;
    wavesRitem->StartIndexLocation = wavesSubmesh.StartIndexLocation;
    wavesRitem->BaseVertexLocation = wavesSubmesh.BaseVertexLocation;

    mWavesRitem = wavesRitem.get();

    mRitemLayer[(int)RenderLayer::Opaque].push_back(boxRitem.get());
    mRitemLayer[(int)RenderLayer::Opaque].push_back(landRitem.get());
    mRitemLayer[(int)RenderLayer::Opaque].push_back(wavesRitem.get());

    mAllRitems.push_back(std::move(boxRitem));
    mAllRitems.push_back(std::move(landRitem));
    mAllRitems.push_back(std::move(wavesRitem));
}

void TexWavesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void TexWavesApp::BuildPSOs()
{
    //
    // PSO for opaque objects.
    //

    const D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc = {
        .pRootSignature = mRootSignature.Get(),
        .VS = {
            reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
            mShaders["standardVS"]->GetBufferSize() },
        .PS = {
            reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
            mShaders["opaquePS"]->GetBufferSize() },
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
    // PSO for opaque wireframe objects.
    //

    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
    opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;

    md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc,
        IID_PPV_ARGS(&mPSOs["opaqueWireframe"])) >> chk;
}

void TexWavesApp::AnimateMaterials(const GameTimer& gt)
{
    auto waterMat = mMaterials["water"].get();
    float& tu = waterMat->MatTransform(3, 0);
    float& tv = waterMat->MatTransform(3, 1);

    tu += 0.1f * gt.DeltaTime();
    tv += 0.02f * gt.DeltaTime();

    if (tu >= 1.f) tu -= 1.f;
    if (tv >= 1.f) tv -= 1.f;

    // Material has changed, we should update the constant buffer of each frame resource.
    // Otherwise we not need to update.
    waterMat->NumFramesDirty = gNumFrameResources;
}

void TexWavesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    for (auto& e : mAllRitems)
    {
        // Only update the cbuffer data if the constants have changed.  
        // This needs to be tracked per frame resource.
        if (e->NumFramesDirty > 0)
        {
            ObjectConstants objConstants;
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
            XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void TexWavesApp::UpdateMaterialCBs(const GameTimer& gt)
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

void TexWavesApp::UpdateMainPassCB(const GameTimer& gt)
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
    mMainPassCB.gAmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };

    //XMVECTOR lightDir = -MathHelper::SphericalToCartesian(1.f, mSunTheta, mSunPhi);
    //XMStoreFloat3(&mMainPassCB.Lights[0].Direction, lightDir);
    //mMainPassCB.Lights[0].Strength = { 1.f, 1.f, 0.9f };

    mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[0].Strength = { 0.9f, 0.9f, 0.9f };
    mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    mMainPassCB.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };
    mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void TexWavesApp::UpdateSunPosition(const GameTimer& gt)
{
    const float dt = gt.DeltaTime();

    if (GetAsyncKeyState(VK_LEFT) & 0x8000)
        mSunTheta -= 1.0f * dt;

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000)
        mSunTheta += 1.0f * dt;

    if (GetAsyncKeyState(VK_UP) & 0x8000)
        mSunPhi -= 1.0f * dt;

    if (GetAsyncKeyState(VK_DOWN) & 0x8000)
        mSunPhi += 1.0f * dt;

    mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV2);
}

void TexWavesApp::UpdateWaves(const GameTimer& gt)
{
    // Create a random wave each 0.25s
    static float t_base = 0.f;
    if ((mTimer.TotalTime() - t_base) >= 0.25f)
    {
        t_base += 0.25f;

        int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
        int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

        float r = MathHelper::RandF(0.2f, 0.5f);

        mWaves->Disturb(i, j, r);
    }

    // Update simulated wave -- [Algorithm]
    mWaves->Update(gt.DeltaTime());

    // Update the wave vertex buffer with the new solution.
    auto currWavesVB = mCurrFrameResource->WavesVB.get();
    for (int i = 0; i < mWaves->VertexCount(); ++i)
    {
        Vertex v;

        v.Pos = mWaves->Position(i);
        v.Normal = mWaves->Normal(i);

        // Derive tex-coords from position by mapping [-w/2,w/2] --> [0,1]
        // No need to update actually since only wave's y component changes.
        v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
        v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

        currWavesVB->CopyData(i, v);
    }

    // Set the dynamic VB of the wave renderitem to the current frame VB.
    mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void TexWavesApp::DrawRenderItems(const std::vector<RenderItem*>& ritems)
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

float TexWavesApp::GetHillsHeight(float x, float z) const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 TexWavesApp::GetHillsNormal(float x, float z) const
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

std::array<const D3D12_STATIC_SAMPLER_DESC, 6> TexWavesApp::GetStaticSamplers()
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

int WINAPI
WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ PSTR pCmdLine, _In_ int nCmdShow)
{
    try {
        TexWavesApp app(hInstance);
        if (!app.Initialize()) { return 0; }
        return app.Run();
    }
    catch (std::exception e) {
        MessageBoxA(nullptr, e.what(), "Graphics Error", MB_OK);
        return 0;
    }
}