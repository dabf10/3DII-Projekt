// ############################################################################
// Output the diffuse light (which may be colored), to the rgb channels, and
// the specular light (which will always be considered white), to the alpha channel.
// ############################################################################

float3 gLightDirectionVS;
float3 gLightColor;
float gProjA;
float gProjB;
float4x4 gInvProj;

// G-Buffer textures
Texture2D gColorMap; // Diffuse + specular intensity in alpha
Texture2D gNormalMap; // Normals + specular power in alpha
Texture2D gDepthMap;

SamplerState gColorSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gDepthSampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gNormalSampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
	float3 ViewRay : POSITION;
};

VS_OUT VS( uint VertexID : SV_VertexID )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH.x = (VertexID == 2) ? 3.0f : -1.0f;
	output.PosH.y = (VertexID == 0) ? -3.0f : 1.0f;
	output.PosH.zw = 1.0f;

	output.TexC = output.PosH.xy * float2(0.5f, -0.5f) + 0.5f;

	float3 posVS = mul(output.PosH, gInvProj).xyz;
	// Clamp view ray to plane at Z = 1. For a directional light, this can
	// be done in the vertex shader because we only interpolate in the XY
	// direction (screen aligned)
	output.ViewRay = float3(posVS.xy / posVS.z, 1.0f);

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Get the data we need out of the G-Buffer.

	float4 normalData = gNormalMap.Sample( gNormalSampler, input.TexC );
	// Transform normal back into [-1,1] range
	float3 normal = 2.0f * normalData.xyz - 1.0f;
	// Get specular power, and transform into [0,255] range
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap.
	float4 color = gColorMap.Sample( gColorSampler, input.TexC );
	float specularIntensity = color.a;
	//float specularIntensity = gColorMap.Sample( gColorSampler, input.TexC ).a;

	// For specular lighting, we need to have the vector from the camera to the
	// point being shaded, alas, we need the position. Right now we have the
	// depth in gDepthMap, which can be used to construct view space position.

	// Read depth
	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;
	float linearDepth = gProjB / (depth - gProjA);
	float3 posVS = input.ViewRay * linearDepth;

	// After we compute the vector from the surface to the light (which in this
	// case is the negated gLightDirection), we compute the diffuse light with
	// the dot product between the normal and the light vector. The specular light
	// is computed using the dot product between the light reflection vector and
	// the camera-to-object vector. The output will contain the diffuse light in
	// the RGB channels, and the specular light in the A channel.

	// Surface-to-light
	float3 lightVector = -normalize(gLightDirectionVS);

	// Compute diffuse light
	float NdL = max(0, dot(normal, lightVector));

	float3 diffuseLight = NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-lightVector, normal));

	// Camera-to-surface vector (camera position is origin because of view space :) )
	float3 directionToCamera = normalize(-posVS.xyz);

	// Compute specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float y = specularPower;
	float specularLight = specularIntensity * pow(x, y);

	// ------------------------------------------

	float3 ambientLight = float3( 0.3f, 0.3f, 0.3f );
	return float4( color.rgb * (diffuseLight + ambientLight) + specularLight, 1 );
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