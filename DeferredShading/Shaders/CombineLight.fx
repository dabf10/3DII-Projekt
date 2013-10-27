// ############################################################################
// Calculates lit color by sampling diffuse color (from G-Buffer) and light
// intensity (from light map aquired by additively rendering lights). This data
// is combined in order to calculate a lit pixel.
// ############################################################################

Texture2D gColorMap;
Texture2D gLightMap;
RWTexture2D<unorm float4> gComposite; // For compute shader

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

float4 PS( VS_OUT input ) : SV_TARGET
{
	float3 diffuseColor = gColorMap.Sample( gColorSampler, input.TexC ).rgb;

	float4 light = gLightMap.Sample( gLightSampler, input.TexC );

	float3 diffuseLight = light.rgb;
	float specularLight = light.a;

	return float4((diffuseColor * diffuseLight + specularLight), 1);
	//return float4((diffuseColor * diffuseLight * specularLight), 1); // Intressant effekt :)
}

[numthreads(16, 16, 1)]
void CS( int3 dispatchThreadID : SV_DispatchThreadID )
{
	float3 diffuseColor = gColorMap[dispatchThreadID.xy].rgb;
	float4 light = gLightMap[dispatchThreadID.xy];
	float3 diffuseLight = light.rgb;
	float specularLight = light.a;
	
	gComposite[dispatchThreadID.xy] = float4((diffuseColor * diffuseLight + specularLight), 1);
	//gComposite[dispatchThreadID.xy] = float4((diffuseColor * diffuseLight * specularLight), 1); // Intressant effekt :)
}

technique11 FullscreenUsingPixelShader
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS() ) );
	}
}

technique11 UsingComputeShader
{
	pass p0
	{
		SetVertexShader( NULL );
		SetGeometryShader( NULL );
		SetPixelShader( NULL );
		SetComputeShader( CompileShader( cs_5_0, CS() ) );
	}
}