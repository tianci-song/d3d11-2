#pragma once

#include <windows.h>
#include <string>
#include <wrl.h>
#include <dxgidebug.h>
#include <vector>
#include <source_location>
#include <format>

extern class DxgiInfoManager dxgiInfoManager;
extern struct CheckerToken chk;

class DxgiInfoManager {
public:
	DxgiInfoManager();
	~DxgiInfoManager() {}
	bool ErrorDetected();
	void SavePrevNumStoreMessages();
	std::string ErrorInfo();
private:
	Microsoft::WRL::ComPtr<IDXGIInfoQueue> mDxgiInfoQueue;
	UINT64 prevNumStoredMessages = 0;
};

struct CheckerToken {};
struct HrGrabber {
	HrGrabber(unsigned int hr, std::source_location = std::source_location::current()) noexcept;
	unsigned int _hr;
	std::source_location _loc;
};

void operator>>(HrGrabber, CheckerToken);


// Macros for functions return none
#ifndef ThrowIfFailed_VOID
#define ThrowIfFailed_VOID(x)												\
{																			\
	dxgiInfoManager.SavePrevNumStoreMessages();								\
	(x);																	\
	if (dxgiInfoManager.ErrorDetected())									\
	{																		\
	throw std::runtime_error(												\
	std::format("file: {}\nline: {}\nfunction: {}\nerror: {}\n",			\
	__FILE__,																\
	__LINE__,																\
	#x,																		\
	dxgiInfoManager.ErrorInfo()));											\
	}																		\
}
#endif