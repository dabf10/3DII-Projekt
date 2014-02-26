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

Texture2D<float4> gHDRTexture : register( t0 );
StructuredBuffer<float> gAvgLum : register( t1 );

// TODO: Ta bort LumWhiteSqr och ersätt med en StructuredBuffer<float> gMaxLum : register( t2 );
cbuffer FinalPassConstants : register( b0 )
{
	float gMiddleGrey : packoffset( c0 );
	float LumWhiteSqr : packoffset( c0.y );
}

static const float3 LUM_FACTOR = float3( 0.299, 0.587, 0.114 );
static const float3x3 rgbToXYZ = float3x3( 0.4124, 0.2126, 0.0193,
										   0.3576, 0.7152, 0.1192,
										   0.1805, 0.0722, 0.9505 );
static const float3x3 xyzToRGB = float3x3( 3.2406, -0.9689, 0.0557,
										   -1.5372, 1.8758, -0.2040,
										   -0.4986, 0.0415, 1.0570 );

float3 ToneMap( float3 HDRColor )
{
	// Luminance of current pixel
	float luminance = dot( HDRColor, LUM_FACTOR );

	// Apply Reinhard tone mapping
	float scaledLuminance = luminance * gMiddleGrey / gAvgLum[0];
	scaledLuminance = scaledLuminance * (1 + scaledLuminance / (LumWhiteSqr)) / (1 + scaledLuminance);

	// Apply scale to xyY color space where Y is luminosity and xy is chromaticity
	// (eyes do not map red, green, and blue evenly). Convert RGB -> XYZ -> xyY:
	float3 xyz = mul( HDRColor, rgbToXYZ );
	float3 xyY = float3( xyz.x / (xyz.x + xyz.y + xyz.z),
						 xyz.y / (xyz.x + xyz.y + xyz.z),
						 xyz.y );

	// Now we can apply luminance.
	xyY.z *= scaledLuminance;

	// Convert back to RGB:
	xyz = float3( xyY.z * xyY.x / xyY.y,
				  xyY.z,
				  xyY.z * (1.0 - xyY.x - xyY.y) / xyY.y );
	float3 rgb = mul( xyz, xyzToRGB );

	return rgb;
}

float4 FinalPassPS( VS_OUT input ) : SV_TARGET
{
	// Get the color sample
	float3 color = gHDRTexture.Load( uint3( input.PosH.xy, 0 ) ).xyz;

	// Tone mapping
	color = ToneMap( color );

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