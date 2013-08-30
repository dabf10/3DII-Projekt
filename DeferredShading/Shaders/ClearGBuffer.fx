// ############################################################################
// Simply outputs data to clear G-Buffer render targets to default values.
// ############################################################################

struct VS_IN
{
	float3 Pos : POSITION0;
	float2 TexC : TEXCOORD; // Not used, but to be able to use the same VB as texquad
};

struct VS_OUT
{
	float4 Pos : SV_POSITION;
};

VS_OUT VS( VS_IN input )
{
	// Just pass position on, it's already in NDC space.
	VS_OUT output = (VS_OUT)0;

	output.Pos = float4(input.Pos, 1.0f);

	return output;
}

struct PS_OUT
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
};

PS_OUT PS( VS_OUT input )
{
	PS_OUT output = (PS_OUT)0;

	// Black color
	output.Color = 0.0f;

	// When transforming 0.5f into [-1,1], we will get 0.0f
	output.Normal.rgb = 0.5f;

	// No specular power
	output.Normal.a = 0.0f;

	return output;
}

technique11 Technique1
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS() ) );
	}
}