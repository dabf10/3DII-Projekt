// ############################################################################
// Output the diffuse light (which may be colored), to the rgb channels, and
// the specular light (which will always be considered white), to the alpha channel.
// ############################################################################

float4x4 gProj;
float4x4 gInvProj;

float3 gLightDirectionVS;
float3 gLightColor;
float gLightIntensity;

// G-Buffer textures
Texture2D gColorMap; // Diffuse + specular intensity in alpha
Texture2D gNormalMap; // Normals + specular power in alpha
Texture2D gDepthMap;

struct GBuffer
{
	float3 Diffuse;
	float3 Normal;
	float3 PosVS;
	float SpecularIntensity;
	float SpecularPower;
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

float3 EvaluateDirectionalLight( GBuffer gbuffer )
{
	// Surface-to-light
	float3 lightVector = -normalize(gLightDirectionVS);

	// Compute diffuse light
	float NdL = gLightIntensity * max(0, dot(gbuffer.Normal, lightVector));

	float3 diffuseLight = NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-lightVector, gbuffer.Normal));

	// Camera-to-surface vector (camera position is origin because of view space :) )
	float3 directionToCamera = normalize(-gbuffer.PosVS.xyz);

	// Compute specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);

	return diffuseLight + specularLight;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	GBuffer gbuffer;

	float depth = gDepthMap.Load( uint3( input.PosH.xy, 0 ) ).r;
	float4 diffuse_specIntensity = gColorMap.Load( uint3( input.PosH.xy, 0 ) );
	float4 normal_specPower = gNormalMap.Load( uint3( input.PosH.xy, 0 ) );

	// Reconstruct view space position
	float linearDepth = gProj[3][2] / (depth - gProj[2][2]);
	float3 posVS = input.ViewRay * linearDepth;

	gbuffer.PosVS = posVS;
	gbuffer.Diffuse = diffuse_specIntensity.rgb;
	gbuffer.Normal = normalize(2.0f * normal_specPower.xyz - 1.0f); // Transform back into [-1,1] range
	gbuffer.SpecularIntensity = diffuse_specIntensity.a;
	gbuffer.SpecularPower = normal_specPower.a * 255;

	// ------------------------------------------

	return float4( gbuffer.Diffuse * EvaluateDirectionalLight( gbuffer ), 1 );
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