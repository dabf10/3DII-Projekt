struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

VS_OUT VS( uint VertexID : SV_VertexID )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH.x = (VertexID == 2) ? 3.0f : -1.0f;
	output.PosH.y = (VertexID == 0) ? -3.0f : 1.0f;
	output.PosH.zw = 1.0f;

	output.TexC = output.PosH.xy * float2(0.5f, -0.5f) + 0.5f;

	return output;
}

Texture2D<float4> HDRTexture : register( t0 );
StructuredBuffer<float> AvgLum : register( t1 );

SamplerState PointSampler : register( s0 )
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

cbuffer FinalPassConstants : register( b0 )
{
	float MiddleGrey : packoffset( c0 );
	float LumWhiteSqr : packoffset( c0.y );
}

static const float3 LUM_FACTOR = float3(0.299, 0.587, 0.114);

float3 ToneMapping(float3 HDRColor)
{
	// Find the luminance scale for the current pixel
	float LScale = dot(HDRColor, LUM_FACTOR);
	LScale *= MiddleGrey / AvgLum[0];
	LScale = (LScale + LScale * LScale / LumWhiteSqr) / (1.0 + LScale);

	// Apply the luminance scale to the pixels color
	return HDRColor * LScale;
}

float4 FinalPassPS( VS_OUT input ) : SV_TARGET
{
	// Get the color sample
	float3 color = HDRTexture.Sample( PointSampler, input.TexC.xy ).xyz;

	// Tone mapping
	color = ToneMapping(color);

	// Output the LDR value
	return float4(color, 1.0);
}

technique11 Tech
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, FinalPassPS() ) );
	}
}