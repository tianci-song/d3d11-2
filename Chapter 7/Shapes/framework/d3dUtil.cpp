#include "d3dUtil.h"

DxgiInfoManager dxgiInfoManager;
CheckerToken chk;

ComPtr<ID3D12Resource> d3dUtil::CreateDefaultBuffer(
    ID3D12Device* device, 
    ID3D12GraphicsCommandList* cmdList, 
    const void* initData, 
    UINT64 byteSize, 
    ComPtr<ID3D12Resource>& uploadBuffer)
{
    ComPtr<ID3D12Resource> defaultBuffer;
    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&defaultBuffer) ) >> chk;

    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer) ) >> chk;

    D3D12_SUBRESOURCE_DATA subResourceData = {
        .pData = initData,
        .RowPitch = (LONG_PTR)byteSize,
        .SlicePitch = (LONG_PTR)byteSize
    };

    ThrowIfFailed_VOID(cmdList->ResourceBarrier(
        1u,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_COPY_DEST )));

    UpdateSubresources<1>(cmdList, 
        defaultBuffer.Get(), uploadBuffer.Get(),
        0u, 0u, 1u, &subResourceData );

    ThrowIfFailed_VOID(cmdList->ResourceBarrier(
        1u,
        &CD3DX12_RESOURCE_BARRIER::Transition(
            defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ )));

    return defaultBuffer;
}

ComPtr<ID3DBlob> d3dUtil::CompileShader(
    const std::wstring& filename,
    const D3D_SHADER_MACRO* defines,
    const std::string& entrypoint,
    const std::string& target)
{
    UINT compileFlags = 0;

#if defined (DEBUG) || defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> byteCode;
    ComPtr<ID3DBlob> errors;

    D3DCompileFromFile(
        filename.c_str(), 
        defines,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entrypoint.c_str(),
        target.c_str(),
        compileFlags,
        0u,
        &byteCode,
        &errors) >> chk;

    if (errors != nullptr)
    {
        OutputDebugStringA((char*)errors.Get()->GetBufferPointer());
    }

    return byteCode;
}

ComPtr<ID3DBlob> d3dUtil::LoadBinary(const std::wstring& filename)
{
    std::ifstream fin(filename, std::ios::binary);

    fin.seekg(0, std::ios_base::end);
    std::ifstream::pos_type size = (int)fin.tellg();
    fin.seekg(0, std::ios_base::beg);

    ComPtr<ID3DBlob> blob;
    D3DCreateBlob(size, blob.GetAddressOf()) >> chk;

    fin.read((char*)blob->GetBufferPointer(), size);
    fin.close();

    return blob;
}

DxgiInfoManager::DxgiInfoManager()
{
            /* Code copy from chili hw3d */
   
    // define function signature of DXGIGetDebugInterface
    typedef HRESULT(WINAPI* DXGIGetDebugInterface)(REFIID, void**);

    // load the dll that contains the function DXGIGetDebugInterface
    const auto hModDxgiDebug = LoadLibraryEx(L"dxgidebug.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(hModDxgiDebug);

    // get address of DXGIGetDebugInterface in dll
    const auto DxgiGetDebugInterface = reinterpret_cast<DXGIGetDebugInterface>(
        GetProcAddress(hModDxgiDebug, "DXGIGetDebugInterface"));

    // initialize DxgiInfoQueue
    DxgiGetDebugInterface(IID_PPV_ARGS(&mDxgiInfoQueue));
}

bool DxgiInfoManager::ErrorDetected()
{
    return prevNumStoredMessages < mDxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
}

void DxgiInfoManager::SavePrevNumStoreMessages()
{
    prevNumStoredMessages = mDxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
}

std::string DxgiInfoManager::ErrorInfo()
{
    std::ostringstream oss;

    const auto n = mDxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);

    for (auto i = prevNumStoredMessages; i < n; i++)
    {
        SIZE_T messageLength = 0;

        // Get the size of message i in bytes
        mDxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &messageLength);

        // Allocate memory for message
        auto bytes = std::make_unique<byte[]>(messageLength);
        auto pMessage = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(bytes.get());

        // Get the message and push it to the queue
        mDxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, pMessage, &messageLength);
        oss << i - prevNumStoredMessages + 1 << ". " << pMessage->pDescription << std::endl;
    }

    return oss.str();
}

HrGrabber::HrGrabber(unsigned int hr, std::source_location loc) noexcept :
    _hr(hr), _loc(loc)
{
}

void operator>>(HrGrabber g, CheckerToken)
{
    if (FAILED(g._hr))
    {
        throw std::runtime_error(std::format("[File]: {}\n[Line]: {}\n[Function]: {}\n[Error Info]:\n{}", 
            g._loc.file_name(), 
            g._loc.line(), 
            g._loc.function_name(), 
            dxgiInfoManager.ErrorInfo()));
    }
}
