#include <comdef.h>
#include <memory>
#include <sstream>
#include "d3dUtil.h"

DxgiInfoManager dxgiInfoManager;
CheckerToken chk;

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
        throw std::runtime_error(std::format("file: {}\nline: {}\nfunction: {}\nerror: {}\n", 
            g._loc.file_name(), 
            g._loc.line(), 
            g._loc.function_name(), 
            dxgiInfoManager.ErrorInfo()));
    }
}
