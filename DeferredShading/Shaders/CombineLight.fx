// ############################################################################
// Calculates lit color by sampling diffuse color (from G-Buffer) and light
// intensity (from light map aquired by additively rendering lights). This data
// is combined in order to calculate a lit pixel.
// ############################################################################

Texture2D gColorMap;
Texture2D gLightMap;
Texture2D gAOMap;

SamplerState gSamLinear
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
	float3 diffuseColor = gColorMap.Load( uint3( input.PosH.xy, 0 ) ).rgb;

	float4 light = gLightMap.Load( uint3( input.PosH.xy, 0 ) );

	float3 diffuseLight = light.rgb;
	float specularLight = light.a;

	float ao = gAOMap.Sample( gSamLinear, input.TexC ).r;

	float ambient = 0.3f;
	float3 ambientLight = float3( ambient, ambient, ambient );
	float3 finalColor = diffuseColor * (diffuseLight + ambientLight) + specularLight;

	// TODO: Det verkar som att man borde kunna låta ljusshaders returnera
	// (diffus som vanligt) + specularLight * lightcolor. Då returneras EN färg
	// och specularLight behöver inte finnas med i lightmap överhuvudtaget.
	// Det sparar in fjärde kanalen (räcker med RGB) samt att specularlight får
	// ljusets färg och inte alltid vit. I denna shadern kommer det betyda att
	// man bara läser in ljus från lightmap som rgb (inget spec i a) som man
	// multiplicerar med gbufferns färg rakt av. Ljusshaders kommer behöva anpassas
	// så de returnerar float3
	return float4( ao * finalColor, 1 );
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