float gTexelWidth;
float gTexelHeight;
float gWeights[11] = {
	0.05f, 0.05f, 0.1f, 0.1f, 0.1f, 0.2f, 0.1f, 0.1f, 0.1f, 0.05f, 0.05f
};
static const int gBlurRadius = 5;

Texture2D gNormalMap;
Texture2D gDepthMap;
Texture2D gImageToBlur;

SamplerState gNormalSampler
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gDepthSampler
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gImageToBlurSampler
{
	Filter = MIN_MAG_LINEAR_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

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

// Performs an edge-preserving blur by sampling neighbor fragments on either side
// of the rasterized fragment horizontally or vertically. The neighbor fragments
// are summed together using a weight, which results in a blurred fragment.
float4 PS( VS_OUT input, uniform bool gHorizontalBlur ) : SV_TARGET
{
	float2 texOffset;
	if (gHorizontalBlur)
		texOffset = float2(gTexelWidth, 0.0f);
	else
		texOffset = float2(0.0f, gTexelHeight);

	// The center value always contributes to the sum.
	float4 color = gWeights[5] * gImageToBlur.SampleLevel( gImageToBlurSampler, input.TexC, 0.0f );
	float totalWeight = gWeights[5];

	float3 centerNormal = gNormalMap.SampleLevel( gNormalSampler, input.TexC, 0.0f ).xyz * 2.0f - 1.0f;
	float centerDepth = gDepthMap.SampleLevel( gDepthSampler, input.TexC, 0.0f ).r;

	// Loop through pixels in one end of the line to the other.
	for (float i = -gBlurRadius; i <= gBlurRadius; ++i)
	{
		// We already added in the center weight.
		if (i == 0)
			continue;

		float2 tex = input.TexC + i * texOffset;

		float3 neighborNormal = gNormalMap.SampleLevel( gNormalSampler, tex, 0.0f ).xyz * 2.0f - 1.0f;
		float neighborDepth = gDepthMap.SampleLevel( gDepthSampler, tex, 0.0f ).r;

		// If the center value and neighbor values differ too much (either in
		// normal or depth), then we assume we are sampling across a discontinuity.
		// We discard such samples from the blur.

		if (dot(neighborNormal, centerNormal) >= 0.8f &&
			abs(neighborDepth - centerDepth) <= 0.2)
		{
			float weight = gWeights[i+gBlurRadius];

			// Add neighbor pixel to blur.
			color += weight * gImageToBlur.SampleLevel( gImageToBlurSampler, tex, 0.0f );
			totalWeight += weight;
		}
	}

	// Compensate for discarded samples by making total weights sum to 1.
	return color / totalWeight;
}

technique11 HorizontalBlur
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS(true) ) );
	}
}

technique11 VerticalBlur
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS(false) ) );
	}
}