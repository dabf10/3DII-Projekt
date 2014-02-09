cbuffer cbPerObject
{
	float4x4 gWVP;
	float4x4 gWorldViewInvTrp;
};

float gSpecularIntensity = 0.8f;
float gSpecularPower = 0.5f;

Texture2D gDiffuseMap;

SamplerState gTriLinearSam
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = WRAP;
	AddressV = WRAP;
};

struct VS_IN
{
	float3 PosL : POSITION;
	float2 TexC : TEXCOORD;
	float3 NormL : NORMAL;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
	float3 NormVS : NORMAL;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul(float4(input.PosL, 1.0f), gWVP);
	output.TexC = input.TexC;
	output.NormVS = mul(float4(input.NormL, 0.0f), gWorldViewInvTrp).xyz;

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

	float4 diffuse = gDiffuseMap.Sample( gTriLinearSam, input.TexC );
	float gamma = 2.2f;
	output.Color.rgb = pow( abs(diffuse.rgb), gamma );
	output.Color.a = gSpecularIntensity;

	// Transform normal from [-1,1] to [0,1] because RT store in [0,1] domain.
	output.Normal.rgb = 0.5f * (normalize(input.NormVS) + 1.0f);
	output.Normal.a = gSpecularPower;

	return output;
}

technique11 DefaultTech
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS() ) );
	}
}