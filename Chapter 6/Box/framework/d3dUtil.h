#pragma once

#include <cassert>
#include <windows.h>
#include <WindowsX.h>
#include <string>
#include <memory>
#include <sstream>
#include <fstream>
#include <wrl.h>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <vector>
#include <unordered_map>
#include <source_location>
#include <format>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <directxcollision.h>
#include "MathHelper.h"
#include "d3dx12.h"

namespace wrl = Microsoft::WRL;
namespace dx = DirectX;

extern class DxgiInfoManager dxgiInfoManager;
extern struct CheckerToken chk;

class d3dUtil {
public:
	static wrl::ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		wrl::ComPtr<ID3D12Resource>& uploadBuffer);

	static UINT CalculateConstantBufferByteSize(UINT byteSize)
	{
		// Constant buffers must be a multiple of the minimum hardware
		// allocation size (usually 256 bytes).  So round up to nearest
		// multiple of 256.  We do this by adding 255 and then masking off
		// the lower 2 bytes which store all bits < 256.
		// Example: Suppose byteSize = 300.
		// (300 + 255) & ~255
		// 555 & ~255
		// 0x022B & ~0x00ff
		// 0x022B & 0xff00
		// 0x0200
		// 512
		return (byteSize + 255) & ~255;
	}

	static wrl::ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);

	static wrl::ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);
};

struct SubmeshGeometry {

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	UINT BaseVertexLocation = 0;

	dx::BoundingBox Bounds;
};

struct MeshGeometry {

	std::string Name;

	wrl::ComPtr<ID3DBlob> VertexBufferCPU;
	wrl::ComPtr<ID3DBlob> IndexBufferCPU;

	wrl::ComPtr<ID3D12Resource> VertexBufferGPU;
	wrl::ComPtr<ID3D12Resource> IndexBufferGPU;

	wrl::ComPtr<ID3D12Resource> VertexBufferUploader;
	wrl::ComPtr<ID3D12Resource> IndexBufferUploader;

	UINT VertexByteStride = 0;
	UINT VertexBufferByteSize = 0;
	UINT IndexBufferByteSize = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;

	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv = {
			.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress(),
			.SizeInBytes = VertexBufferByteSize,
			.StrideInBytes = VertexByteStride,
		};
		return vbv;
	}

	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
	{
		D3D12_INDEX_BUFFER_VIEW ibv = {
			.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress(),
			.SizeInBytes = IndexBufferByteSize,
			.Format = IndexFormat,
		};
		return ibv;
	}
};

class DxgiInfoManager {
public:
	DxgiInfoManager();
	~DxgiInfoManager() {}
	bool ErrorDetected();
	void SavePrevNumStoreMessages();
	std::string ErrorInfo();
private:
	wrl::ComPtr<IDXGIInfoQueue> mDxgiInfoQueue;
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
	throw std::runtime_error(std::format(									\
		"[File]: {}\n[Line]: {}\n[Function]: {}\n[Error Info]:\n{}",		\
		__FILE__,															\
		__LINE__,															\
		#x,																	\
		dxgiInfoManager.ErrorInfo()));										\
	}																		\
}
#endif

class Input {
public:
// If the desired virtual key is a letter or digit(A through Z, a through z, or 0 through 9),
// nVirtKey must be set to the ASCII value of that character. For other keys, it must be a 
// virtual key code.
// If a non-English keyboard layout is used, virtual keys with values in the range ASCII A
// through Z and 0 through 9 are used to specify most of the character keys, here is 0x20.
	static bool IsKeyPressed(int key)
	{
		if (isdigit(key) || isalpha(key))
		{
			return GetKeyState(key - 0x20) & 0x8000;
		}

		return GetKeyState(key) & 0x8000;
	}
};