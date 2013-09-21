cbuffer cbPerFrame
{
	float4x4 gLightWVP;
};

struct VS_IN
{
	float3 PosL : POSITION;
	float2 TexC : TEXCOORD;
	float3 NormalL : NORMAL;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul(float4(input.PosL, 1.0f), gLightWVP);
	output.TexC = input.TexC;

	return output;
}

technique11 BuildShadowMapTech
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_4_0, VS()));
		SetGeometryShader(NULL);
		SetPixelShader(NULL);
	}
}