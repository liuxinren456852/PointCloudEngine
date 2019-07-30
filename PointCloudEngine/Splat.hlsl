#include "SplatBlending.hlsl"

cbuffer SplatRendererConstantBuffer : register(b0)
{
    float4x4 World;
//------------------------------------------------------------------------------ (64 byte boundary)
    float4x4 View;
//------------------------------------------------------------------------------ (64 byte boundary)
    float4x4 Projection;
//------------------------------------------------------------------------------ (64 byte boundary)
    float4x4 WorldInverseTranspose;
//------------------------------------------------------------------------------ (64 byte boundary)
	float4x4 WorldViewProjectionInverse;
//------------------------------------------------------------------------------ (64 byte boundary)
    float3 cameraPosition;
    float fovAngleY;
//------------------------------------------------------------------------------ (16 byte boundary)
    float samplingRate;
	float blendFactor;
	bool useBlending;
	// 4 bytes auto padding
//------------------------------------------------------------------------------ (16 byte boundary)
};  // Total: 352 bytes with constant buffer packing rules

struct VS_INPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    uint3 color : COLOR;
};

struct VS_OUTPUT
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float3 color : COLOR;
};

VS_OUTPUT VS(VS_INPUT input)
{
    VS_OUTPUT output;
    output.position = mul(float4(input.position, 1), World);
    output.normal = normalize(mul(input.normal, WorldInverseTranspose));
    output.color = input.color / 255.0f;

    return output;
}

[maxvertexcount(6)]
void GS(point VS_OUTPUT input[1], inout TriangleStream<GS_SPLAT_OUTPUT> output)
{
    float3 cameraRight = float3(View[0][0], View[1][0], View[2][0]);
    float3 cameraUp = float3(View[0][1], View[1][1], View[2][1]);
    float3 cameraForward = float3(View[0][2], View[1][2], View[2][2]);

    // Billboard should face in the same direction as the normal
	float splatSizeWorld = length(mul(float3(samplingRate, 0, 0), World).xyz);
    float3 up = 0.5f * splatSizeWorld * normalize(cross(input[0].normal, cameraRight));
    float3 right = 0.5f * splatSizeWorld * normalize(cross(input[0].normal, up));

    float4x4 VP = mul(View, Projection);

	SplatBlendingGS(input[0].position, input[0].normal, input[0].color, up, right, VP, output);
}

float4 PS(GS_SPLAT_OUTPUT input) : SV_TARGET
{
	return SplatBlendingPS(useLighting, useBlending, cameraPosition, blendFactor, WorldViewProjectionInverse, input);
}