/* Vertex shader */

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
}

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
    
    float4 posW = mul(float4(vin.PosLocal, 1.f), gWorld);
    vout.PosHomoClip = mul(posW, gViewProj);
    
    vout.Color = vin.Color;
    
    return vout;
}

/* Pixel shader */

struct PixelIn
{
    float4 posHomoClip : SV_POSITION;
    float4 color : COLOR;
};

float4 PS(PixelIn pin) : SV_TARGET
{
    return pin.color;
}