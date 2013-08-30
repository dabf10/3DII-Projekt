// ############################################################################
// Simple shader used to display a fullscreen texture.
// ############################################################################

Texture2D gTexture;

SamplerState gPointSam
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

void VS(float3 posH : POSITION, float2 texC : TEXCOORD, out float4 oPosH : SV_POSITION, out float2 oTexC : TEXCOORD)
{
	oPosH = float4(posH, 1.0f);
	oTexC = texC;
}

float3 PS(float4 posH : SV_POSITION, float2 texC : TEXCOORD, uniform bool singleChannel) : SV_TARGET
{
	float3 color = gTexture.Sample(gPointSam, texC).rgb;
	
	if (singleChannel)
		return float3(color.r, color.r, color.r);

	return color;
}

technique11 SingleChannel
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_4_0, VS()));
		SetGeometryShader(NULL);
		SetPixelShader(CompileShader(ps_4_0, PS(true)));
	}
}

technique11 MultiChannel
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_4_0, VS()));
		SetGeometryShader(NULL);
		SetPixelShader(CompileShader(ps_4_0, PS(false)));
	}
}