Texture2D gColorMap;
Texture2D gLightMap;

SamplerState gColorSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gLightSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

struct VS_IN
{
	float3 PosH : POSITION;
	float2 TexC : TEXCOORD;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = float4(input.PosH, 1.0f);
	output.TexC = input.TexC;

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	float3 diffuseColor = gColorMap.Sample( gColorSampler, input.TexC ).rgb;

	float4 light = gLightMap.Sample( gLightSampler, input.TexC );

	float3 diffuseLight = light.rgb;
	float specularLight = light.a;

	return float4((diffuseColor * diffuseLight + specularLight), 1);
	//return float4((diffuseColor * diffuseLight * specularLight), 1); // Intressant effekt :)
}

technique11 Technique0
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS() ) );
	}
}