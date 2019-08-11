#include "GroundTruth.hlsl"
#include "LightingConstantBuffer.hlsl"

struct GS_POINT_OUTPUT
{
	float4 position : SV_POSITION;
	float3 positionWorld : POSITION1;
	float3 normal : NORMAL;
	float3 color : COLOR;
};

[maxvertexcount(1)]
void GS(point VS_OUTPUT input[1], inout PointStream<GS_POINT_OUTPUT> output)
{
	float4x4 VP = mul(View, Projection);

	GS_POINT_OUTPUT element;
	element.position = mul(float4(input[0].position, 1), VP);
	element.positionWorld = input[0].position;
	element.normal = input[0].normal;
	element.color = input[0].color;

	output.Append(element);
}

float4 PS(GS_POINT_OUTPUT input) : SV_TARGET
{
	if (normal)
	{
		return float4(0.5f * (input.normal + 1), 1);
	}
	else if (useLighting)
	{
		return float4(PhongLighting(cameraPosition, input.positionWorld, input.normal, input.color), 1);
	}
	else
	{
		return float4(input.color, 1);
	}
}