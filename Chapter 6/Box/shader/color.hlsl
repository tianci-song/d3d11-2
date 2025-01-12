// Vertex shader
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorldViewProj;
    float4x4 Pad0;
    float4x4 Pad1;
    float4x4 Pad2;
};

struct VertexIn
{
    float3 PosLocal : POSITION;
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosHomoClip : SV_POSITION;
    float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    vout.PosHomoClip = mul(float4(vin.PosLocal, 1.f), gWorldViewProj);
    vout.Color = vin.Color;
    
    return vout;
}

// Pixel shader
struct PixelIn
{
    float4 posHomoClip : SV_POSITION;
    float4 color : COLOR;
};

float4 PS(PixelIn pin) : SV_TARGET
{
    return pin.color;
}