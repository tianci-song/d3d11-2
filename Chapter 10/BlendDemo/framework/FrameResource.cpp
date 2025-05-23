#include "FrameResource.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT objectCount,UINT materialCount, UINT waveVertCount)
{
	device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, 
		IID_PPV_ARGS(&CmdListAlloc)) >> chk;

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(
		device, passCount, true);
	ObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(
		device, objectCount, true);
	MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(
		device, materialCount, true);
	WavesVB = std::make_unique<UploadBuffer<Vertex>>(
		device, waveVertCount, false);
}

FrameResource::~FrameResource()
{ }
