#include "framework/App.h"
#include "framework/FrameResource.h"
#include "framework/GeometryGenerator.h"

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

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    int NumFramesDirty = gNumFrameResources;

    // Index into GPU constant buffer corresponding to the ObjectCB for this render item.
    UINT ObjCBIndex = -1;

    // The geometry to be drawn. Note, one geometry may need multiple render items.
    MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    UINT BaseVertexLocation = 0;
};

class ShapesApp : public App {
public:
    ShapesApp(HINSTANCE hInstanceHandle);
    ShapesApp(const ShapesApp&) = delete;
    ShapesApp& operator=(const ShapesApp&) = delete;
    ~ShapesApp() {}

    virtual bool Initialize() override;

private:
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;
    virtual void OnResize() override;

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
    void BuildRenderItems();
    void BuildFrameResources();
    void BuildDescriptorHeaps();
    void BuildConstantBufferViews();
    void BuildPSOs();
    
    void UpdateObjectCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);

    void DrawRenderItems(const std::vector<RenderItem*>& ritems);

private:
    ComPtr<ID3D12RootSignature> mRootSignature;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;   

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    // List of all the render items.
    std::vector<std::unique_ptr<RenderItem>> mAllRitems;

    // Render items divided by PSO.
    std::vector<RenderItem*> mOpaqueRitems;
    std::vector<RenderItem*> mTransparentRitems;

    // Pack the data to be transfered to the GPU constant buffer.
    PassConstants mMainPassCB;
    UINT mPassCbvOffset = 0;
    
    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;
    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;  

    bool mIsWireFrame = true;
};

ShapesApp::ShapesApp(HINSTANCE hInstanceHandle) :
    App(hInstanceHandle)
{
}

bool ShapesApp::Initialize()
{
    if (!App::Initialize()) { return false; }

    // Reset the command list to prep for initialization commands.
    mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr) >> chk;

    // Create resources: desc heaps, cbv, root signature, etc.
    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    BuildConstantBufferViews();
    BuildPSOs();

    // Execute the initialization commands.
    mCommandList->Close() >> chk;
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->ObjectCB.get();

    // Nested loop, each frame do the same operation on each render item.
    for (auto& e : mAllRitems)
    {
        if (e->NumFramesDirty > 0)
        {
            // Only update the cbuffer data if the constants have changed.  
            // This needs to be tracked per frame resource.
            ObjectConstants objConstants;
            XMMATRIX world = XMLoadFloat4x4(&e->World);
            XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

            currObjectCB->CopyData(e->ObjCBIndex, objConstants);

            // Next FrameResource need to be updated too.
            e->NumFramesDirty--;
        }
    }
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
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

    auto currPassCB = mCurrFrameResource->PassCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::Update(const GameTimer& gt)
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

    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
}

void ShapesApp::DrawRenderItems(const std::vector<RenderItem*>& ritems)
{
    const UINT objCount = (UINT)ritems.size();
    const UINT objCBByteSize = d3dUtil::CalculateConstantBufferByteSize(
        sizeof(ObjectConstants));   

    for (UINT i = 0; i < objCount; ++i)
    {
        auto ri = ritems[i];

        mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

        UINT cbvIndex = mCurrFrameResourceIndex * objCount + ri->ObjCBIndex;
        auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
            mCbvHeap->GetGPUDescriptorHandleForHeapStart());
        cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

        mCommandList->SetGraphicsRootDescriptorTable(0, cbvHandle);
        mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, 
            ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

void ShapesApp::Draw(const GameTimer& gt)
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

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    UINT passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
    auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        mCbvHeap->GetGPUDescriptorHandleForHeapStart());
    passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
    mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

    DrawRenderItems(mOpaqueRitems);

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

void ShapesApp::OnResize()
{
    App::OnResize();
}

void ShapesApp::BuildRootSignature()
{
    // Shader programs typically require resources as input (constant buffers,
    // textures, samplers).  The root signature defines the resources the shader
    // programs expect.  If we think of the shader programs as a function, and
    // the input resources as function parameters, then the root signature can be
    // thought of as defining the function signature.  

    // Create descriptor table of CBVs.

    CD3DX12_DESCRIPTOR_RANGE cbvTable0;
    cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE cbvTable1;
    cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParamter[2];

    // Create root signature.
    slotRootParamter[0].InitAsDescriptorTable(1, &cbvTable0);
    slotRootParamter[1].InitAsDescriptorTable(1, &cbvTable1);

    // A root signature is an array of root parameters.
    const D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {
        .NumParameters = 2,
        .pParameters = slotRootParamter,
        .NumStaticSamplers = 0,
        .pStaticSamplers = nullptr,
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

void ShapesApp::BuildShadersAndInputLayout()
{
    mInputLayout = 
    {
        {   "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   },
        {   "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0   }
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(L"shader/color.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(L"shader/color.hlsl", nullptr, "PS", "ps_5_1");
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;

    GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.f, 30.f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //
    // We are concatenating all the geometry into one big vertex/index buffer.  So
    // define the regions in the buffer each submesh covers.
    //

    // Cache the vertex offsets to each object in the concatenated vertex buffer.
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    // Cache the starting index for each object in the concatenated index buffer.
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    // Define the SubmeshGeometry that cover different 
    // regions of the vertex/index buffers.

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    //
    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.
    //

    std::vector<Vertex> totalVertices;

    for (const auto& v: box.Vertices)
    {
        totalVertices.emplace_back(v.Position, XMFLOAT4(DirectX::Colors::DarkGreen));
    }

    for (const auto& v : grid.Vertices)
    {
        totalVertices.emplace_back(v.Position, XMFLOAT4(DirectX::Colors::ForestGreen));
    }

    for (const auto& v : sphere.Vertices)
    {
        totalVertices.emplace_back(v.Position, XMFLOAT4(DirectX::Colors::Crimson));
    }

    for (const auto& v : cylinder.Vertices)
    {
        totalVertices.emplace_back(v.Position, XMFLOAT4(DirectX::Colors::SteelBlue));
    }

    std::vector<std::uint16_t> totalIndices;

    totalIndices.insert(totalIndices.end(), 
        std::begin(box.GetIndices16()), 
        std::end(box.GetIndices16()));

    totalIndices.insert(totalIndices.end(),
        std::begin(grid.GetIndices16()),
        std::end(grid.GetIndices16()));

    totalIndices.insert(totalIndices.end(),
        std::begin(sphere.GetIndices16()),
        std::end(sphere.GetIndices16()));

    totalIndices.insert(totalIndices.end(),
        std::begin(cylinder.GetIndices16()),
        std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT)totalVertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)totalIndices.size() * sizeof(uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU) >> chk;
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), totalVertices.data(), vbByteSize);

    D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU) >> chk;
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), totalIndices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        totalVertices.data(),
        vbByteSize,
        geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        totalIndices.data(),
        ibByteSize,
        geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexBufferByteSize = ibByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildRenderItems()
{
    auto boxRitem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRitem->World,
        XMMatrixScaling(2.f, 2.f, 2.f) * XMMatrixTranslation(0.f, 5.f, 0.f));
    boxRitem->ObjCBIndex = 0;
    boxRitem->Geo = mGeometries["shapeGeo"].get();
    boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry boxSubmesh = boxRitem->Geo->DrawArgs["box"];
    boxRitem->IndexCount = boxSubmesh.IndexCount;
    boxRitem->StartIndexLocation = boxSubmesh.StartIndexLocation;
    boxRitem->BaseVertexLocation = boxSubmesh.BaseVertexLocation;
    mAllRitems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
    gridRitem->ObjCBIndex = 1;
    gridRitem->Geo = mGeometries["shapeGeo"].get();
    gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    SubmeshGeometry gridSubmesh = gridRitem->Geo->DrawArgs["grid"];
    gridRitem->IndexCount = gridSubmesh.IndexCount;
    gridRitem->StartIndexLocation = gridSubmesh.StartIndexLocation;
    gridRitem->BaseVertexLocation = gridSubmesh.BaseVertexLocation;
    mAllRitems.push_back(std::move(gridRitem));

    UINT objCBIndex = 2;
    for (int i = 0; i < 5; ++i)
    {
        auto leftCylRitem = std::make_unique<RenderItem>();
        auto rightCylRitem = std::make_unique<RenderItem>();
        auto leftSphereRitem = std::make_unique<RenderItem>();
        auto rightSphereRitem = std::make_unique<RenderItem>();

        XMMATRIX leftCylWorld = XMMatrixTranslation(-5.f, 1.5f, -10.f + i * 5.f);
        XMMATRIX rightCylWorld = XMMatrixTranslation(5.f, 1.5f, -10.f + i * 5.f);

        XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.f, 3.5f, -10.f + i * 5.f);
        XMMATRIX rightSphereWorld = XMMatrixTranslation(5.f, 3.5f, -10.f + i * 5.f);

        XMStoreFloat4x4(&leftCylRitem->World, leftCylWorld);
        leftCylRitem->ObjCBIndex = objCBIndex++;
        leftCylRitem->Geo = mGeometries["shapeGeo"].get();
        leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        SubmeshGeometry leftCylSubmesh = leftCylRitem->Geo->DrawArgs["cylinder"];
        leftCylRitem->IndexCount = leftCylSubmesh.IndexCount;
        leftCylRitem->StartIndexLocation = leftCylSubmesh.StartIndexLocation;
        leftCylRitem->BaseVertexLocation = leftCylSubmesh.BaseVertexLocation;

        XMStoreFloat4x4(&rightCylRitem->World, rightCylWorld);
        rightCylRitem->ObjCBIndex = objCBIndex++;
        rightCylRitem->Geo = mGeometries["shapeGeo"].get();
        rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        SubmeshGeometry rightCylSubmesh = rightCylRitem->Geo->DrawArgs["cylinder"];
        rightCylRitem->IndexCount = rightCylSubmesh.IndexCount;
        rightCylRitem->StartIndexLocation = rightCylSubmesh.StartIndexLocation;
        rightCylRitem->BaseVertexLocation = rightCylSubmesh.BaseVertexLocation;

        XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
        leftSphereRitem->ObjCBIndex = objCBIndex++;
        leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
        leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        SubmeshGeometry leftSphereSubmesh = leftSphereRitem->Geo->DrawArgs["sphere"];
        leftSphereRitem->IndexCount = leftSphereSubmesh.IndexCount;
        leftSphereRitem->StartIndexLocation = leftSphereSubmesh.StartIndexLocation;
        leftSphereRitem->BaseVertexLocation = leftSphereSubmesh.BaseVertexLocation;

        XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
        rightSphereRitem->ObjCBIndex = objCBIndex++;
        rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
        rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        SubmeshGeometry rightSphereSubmesh = rightSphereRitem->Geo->DrawArgs["sphere"];
        rightSphereRitem->IndexCount = rightSphereSubmesh.IndexCount;
        rightSphereRitem->StartIndexLocation = rightSphereSubmesh.StartIndexLocation;
        rightSphereRitem->BaseVertexLocation = rightSphereSubmesh.BaseVertexLocation;

        mAllRitems.push_back(std::move(leftCylRitem));
        mAllRitems.push_back(std::move(rightCylRitem));
        mAllRitems.push_back(std::move(leftSphereRitem));
        mAllRitems.push_back(std::move(rightSphereRitem));
    }

    // All the render items are opaque.
    for (const auto& e : mAllRitems)
    {
        mOpaqueRitems.push_back(e.get());
    }
}

void ShapesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 1, (UINT)mAllRitems.size()));
    }
}

void ShapesApp::BuildDescriptorHeaps()
{
    //
    // Need a CBV descriptor for each object for each frame resource,
    // +1 for the perPass CBV for each frame resource.
    //
    UINT objCount = (UINT)mOpaqueRitems.size();
    UINT numDescriptos = (objCount + 1) * gNumFrameResources;

    mPassCbvOffset = objCount * gNumFrameResources;

    const D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        .NumDescriptors = numDescriptos,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE,
        .NodeMask = 0u,
    };

    md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCbvHeap));
}

void ShapesApp::BuildConstantBufferViews()
{
    const UINT objCBByteSize = d3dUtil::CalculateConstantBufferByteSize(
        sizeof(ObjectConstants));

    const UINT objCount = (UINT)mOpaqueRitems.size();

    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
    {
        auto baseAddress = mFrameResources[frameIndex]->ObjectCB->Resource()->GetGPUVirtualAddress();

        for (UINT i = 0; i < objCount; ++i)
        {           
            auto cbAddress = baseAddress + (UINT64)i * objCBByteSize;

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
                .BufferLocation = cbAddress,
                .SizeInBytes = objCBByteSize,
            };

            int heapIndex = frameIndex * objCount + i;
            auto cbvHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
                mCbvHeap->GetCPUDescriptorHandleForHeapStart());
            cbvHandle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

            md3dDevice->CreateConstantBufferView(&cbvDesc, cbvHandle);
        }
    }

    const UINT passCBByteSize = d3dUtil::CalculateConstantBufferByteSize(
        sizeof(PassConstants));

    for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
    {
        auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
        auto cbAddress = passCB->GetGPUVirtualAddress();

        const D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {
            .BufferLocation = cbAddress,
            .SizeInBytes = passCBByteSize,
        };

        int heapIndex = mPassCbvOffset + frameIndex;
        auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            mCbvHeap->GetCPUDescriptorHandleForHeapStart());
        handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

        md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
    }
}

void ShapesApp::BuildPSOs()
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
            .Quality = m4xMsaaState ? m4xMsaaQuality -1 : 0u },
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

int WINAPI
WinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance,
    _In_ PSTR pCmdLine, _In_ int nCmdShow)
{
    try {
        ShapesApp app(hInstance);
        if (!app.Initialize()) { return 0; }
        return app.Run();
    }
    catch (std::exception e) {
        MessageBoxA(nullptr, e.what(), "Graphics Error", MB_OK);
        return 0;
    }
}