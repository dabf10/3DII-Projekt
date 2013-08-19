// För att lagra linjärt djup (om det är av intresse) kan man lagra en variabel
// gLinearDepth som innehåller (Far - Near) clip värdet. Därefter är djupet
// linearDepth = length(position) / gLinearDepth.
// Beroende på om positionen är world eller view space får man troligen linjärt
// world space depth eller view space depth. Denna beräkning kan troligen göras
// för vertexpunkterna och sedan interpoleras till fragmenten.

cbuffer cbPerObject
{
	float4x4 gWVP;
	float4x4 gWorldInvTranspose;
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
	float2 TexC : TEXCOORD0;
	float3 NormW : NORMAL;
	//float2 Depth : TEXCOORD1;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul(float4(input.PosL, 1.0f), gWVP);
	output.TexC = input.TexC;
	output.NormW = mul(float4(input.NormL, 0.0f), gWorldInvTranspose).xyz;
	//output.Depth.x = output.PosH.z;
	//output.Depth.y = output.PosH.w;

	return output;
}

struct PS_OUT
{
	float4 Color : SV_TARGET0;
	float4 Normal : SV_TARGET1;
	//float4 Depth : SV_TARGET2;
};

PS_OUT PS( VS_OUT input )
{
	PS_OUT output = (PS_OUT)0;

	float4 diffuse = gDiffuseMap.Sample( gTriLinearSam, input.TexC);
	output.Color = diffuse;
	output.Color.a = gSpecularIntensity;

	// Transform normal from [-1,1] to [0,1] because RT store in [0,1] domain.
	output.Normal.rgb = 0.5f * (normalize(input.NormW) + 1.0f);
	output.Normal.a = gSpecularPower;

	//output.Depth = input.Depth.x / input.Depth.y;

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