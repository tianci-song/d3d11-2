#pragma once
#include "d3dUtil.h"
#include "UploadBuffer.h"

struct ObjectConstants 
{
	XMFLOAT4X4 World = MathHelper::Identity4x4();
};

struct PassConstants
{
	XMFLOAT4X4 View = MathHelper::Identity4x4();
	XMFLOAT4X4 InvView = MathHelper::Identity4x4();
	XMFLOAT4X4 Proj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
	XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
	XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
	XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float cbPerObjectPad1 = 0.0f;
	XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
	XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
	float NearZ = 0.0f;
	float FarZ = 0.0f;
	float TotalTime = 0.0f;
	float DeltaTime = 0.0f;
};

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT4 Color;
};

// Stores the resources needed for the CPU to build the command lists
// for a frame.  
struct FrameResource {
public:
	FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount);
	FrameResource(const FrameResource&) = delete;
	FrameResource& operator=(const FrameResource&) = delete;
	~FrameResource();

	// We cannot reset the allocator until the GPU is done processing the commands.
	// So each frame needs their own allocator.
	ComPtr<ID3D12CommandAllocator> CmdListAlloc;

	// We cannot update a cbuffer until the GPU is done processing the commands
	// that reference it.  So each frame needs their own cbuffers.
	std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
	std::unique_ptr<UploadBuffer<ObjectConstants>> ObjectCB = nullptr;

	// Fence value to mark commands up to this fence point.  This lets us
	// check if these frame resources are still in use by the GPU.
	UINT64 Fence = 0;
};
