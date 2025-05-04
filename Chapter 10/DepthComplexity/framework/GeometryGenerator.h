#pragma once
#include <cmath>
#include <vector>
#include <directxmath.h>

using namespace DirectX;

class GeometryGenerator {
public:
	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	struct Vertex
	{
		Vertex():
			Position(0.f, 0.f, 0.f),
			Normal(0.f, 0.f, 0.f),
			TangentU(0.f, 0.f, 0.f),
			TexC(0.f, 0.f) {}
		Vertex(
			const XMFLOAT3& p,
			const XMFLOAT3& n,
			const XMFLOAT3& t,
			const XMFLOAT2& uv) :
			Position(p),
			Normal(n),
			TangentU(t),
			TexC(uv) {}
		Vertex(
			float px, float py, float pz,
			float nx, float ny, float nz,
			float tx, float ty, float tz,
			float u, float v) :
			Position(px, py, pz),
			Normal(nx, ny, nz),
			TangentU(tx, ty, tz),
			TexC(u, v) {}

		XMFLOAT3 Position;
		XMFLOAT3 Normal;
		XMFLOAT3 TangentU;
		XMFLOAT2 TexC;
	};

	struct MeshData
	{
		std::vector<Vertex> Vertices;
		std::vector<uint32> Indices32;

		std::vector<uint16>& GetIndices16()
		{
			if (mIndices16.empty())
			{
				mIndices16.resize(Indices32.size());
				for (size_t i = 0; i < Indices32.size(); ++i)
				{
					mIndices16[i] = static_cast<uint16>(Indices32[i]);
				}
			}

			return mIndices16;
		}

	private:
		std::vector<uint16> mIndices16;
	};

	MeshData CreateCylinder(float bottomRadius, float topRadius, float height, 
		uint32 sliceCount, uint32 stackCount);

	MeshData CreateSphere(float radius, uint32 stackCount, uint32 sliceCount);

	MeshData CreateGeosphere(float radius, uint32 numSubdivisions);

	MeshData CreateBox(float width, float height, float depth, uint32 numSubdivisions);

	MeshData CreateGrid(float width, float depth, uint32 m, uint32 n);

private:
	void BuildCylinderTopCap(float topRadius, float height,
		uint32 sliceCount, uint32 stackCount, MeshData& meshData);

	void BuildCylinderBottomCap(float bottomRadius, float height,
		uint32 sliceCount, uint32 stackCount, MeshData& meshData);

	void Subdivide(MeshData& meshData);

	Vertex MidPoint(const Vertex& v0, const Vertex& v1);
};