#pragma once

#include <cassert>
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
#include <array>
#include <unordered_map>
#include <source_location>
#include <format>
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <directxcollision.h>
#include <limits>
#include "MathHelper.h"
#include "d3dx12.h"

#include <WindowsX.h>
#include <Windows.h>

using namespace Microsoft::WRL;
using namespace DirectX;

extern class DxgiInfoManager dxgiInfoManager;
extern struct CheckerToken chk;
extern const int gNumFrameResources;

constexpr auto MAX_SIZE = (std::numeric_limits<long>::max)();

class d3dUtil {
public:
	static ComPtr<ID3D12Resource> CreateDefaultBuffer(
		ID3D12Device* device,
		ID3D12GraphicsCommandList* cmdList,
		const void* initData,
		UINT64 byteSize,
		ComPtr<ID3D12Resource>& uploadBuffer);

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

	static ComPtr<ID3DBlob> CompileShader(
		const std::wstring& filename,
		const D3D_SHADER_MACRO* defines,
		const std::string& entrypoint,
		const std::string& target);

	static ComPtr<ID3DBlob> LoadBinary(const std::wstring& filename);
};

struct SubmeshGeometry {

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	UINT BaseVertexLocation = 0;

	BoundingBox Bounds;
};

struct MeshGeometry {

	std::string Name;

	ComPtr<ID3DBlob> VertexBufferCPU;
	ComPtr<ID3DBlob> IndexBufferCPU;

	ComPtr<ID3D12Resource> VertexBufferGPU;
	ComPtr<ID3D12Resource> IndexBufferGPU;

	ComPtr<ID3D12Resource> VertexBufferUploader;
	ComPtr<ID3D12Resource> IndexBufferUploader;

	UINT VertexByteStride = 0;
	UINT VertexBufferByteSize = 0;
	UINT IndexBufferByteSize = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R16_UINT;

	// Submesh is not a mesh, it just stores the offset so we can get 
	// the mesh info from the big overall buffer. 
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

// The struct in hlsl is 16-byte aligned, so the layout of variables matters.
struct MaterialConstants {
	XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = 0.25f;

	// Used in texture mapping.
	XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

struct Material {
	// Unique material name for lookup.
	std::string Name;

	// Index into constant buffer corresponding to this material.
	int MatCBIndex = -1;

	// Index into SRV heap for diffuse texture.
	int DiffuseSrvHeapIndex = -1;

	// Index into SRV heap for normal texture.
	int NormalSrvHeapIndex = -1;

	// Dirty flag indicating the material has changed and we need to update the constant buffer.
	// Because we have a material constant buffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify a material we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Material constant buffer data used for shading.
	XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
	XMFLOAT3 FresnelR0 = { 0.01f, 0.01f, 0.01f };
	float Roughness = .25f;
	XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
};

// The Light struct in hlsl is 16-byte aligned, so the layout of variables matters.
struct Light {
	XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
	float FalloffStart = 1.0f;                 // point/spot light only
	XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };// directional/spot light only
	float FalloffEnd = 10.0f;                  // point/spot light only
	XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };  // point/spot light only
	float SpotPower = 64.0f;                   // spot light only
};

constexpr auto MaxLights = 16;

struct Texture {
	std::string Name;
	std::wstring Filename;
	ComPtr<ID3D12Resource> Resource = nullptr;
	ComPtr<ID3D12Resource> UploadHeap = nullptr;
};

class DxgiInfoManager {
public:
	DxgiInfoManager();
	~DxgiInfoManager() {}
	bool ErrorDetected();
	std::string ErrorInfo();
private:
	ComPtr<IDXGIInfoQueue> mDxgiInfoQueue;
	UINT64 prevNumStoredMessages = 0;
};

struct CheckerToken {};
struct HrGrabber {
	HrGrabber(HRESULT hr, std::source_location = std::source_location::current()) noexcept;
	HRESULT _hr;
	std::source_location _loc;
};

void operator>>(HrGrabber, CheckerToken);

// Macros for functions return none
#ifndef ThrowIfFailed_VOID
#define ThrowIfFailed_VOID(x)												\
{																			\
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