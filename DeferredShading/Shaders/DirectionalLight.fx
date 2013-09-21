// ############################################################################
// Output the diffuse light (which may be colored), to the rgb channels, and
// the specular light (which will always be considered white), to the alpha channel.
// ############################################################################

// New for shadow mapping
static const float SHADOW_EPSILON = 0.001f;
Texture2D gShadowMap;
float gShadowMapSize;
float gShadowMapDX;
float4x4 gLightViewVolume;
float4x4 gInvView;

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

// New for shadow mapping.
float CalcShadowFactor(float4 projTexC)
{
	// Complete projection by doing division by w. This takes us from HCS to NDC.
	projTexC.xyz /= projTexC.w;

	// Points outside the light volume are in shadow.
	if (projTexC.x < -1.0f || projTexC.x > 1.0f ||
		projTexC.y < -1.0f || projTexC.y > 1.0f ||
		projTexC.z < 0.0f)
		return 0.0f;

	// Transform from NDC space to texture space (NDC->U/V).
	// [-1,1]X[-1,1] -> [0,1]X[0,1]. v axis is inverted to point in right direction.
	projTexC.x = +0.5f * projTexC.x + 0.5f;
	projTexC.y = -0.5f * projTexC.y + 0.5f;

	// Depth in NDC space.
	float depth = projTexC.z;

	//
	// PCF Filtering
	//

	// Sample shadow map to get nearest depth to light.
	float s0 = gShadowMap.Sample(gDepthSampler, projTexC.xy).r;
	float s1 = gShadowMap.Sample(gDepthSampler, projTexC.xy + float2(gShadowMapDX, 0)).r;
	float s2 = gShadowMap.Sample(gDepthSampler, projTexC.xy + float2(0, gShadowMapDX)).r;
	float s3 = gShadowMap.Sample(gDepthSampler, projTexC.xy + float2(gShadowMapDX, gShadowMapDX)).r;

	// Is the pixel depth <= shadow map value?
	float result0 = depth <= s0 + SHADOW_EPSILON;
	float result1 = depth <= s1 + SHADOW_EPSILON;
	float result2 = depth <= s2 + SHADOW_EPSILON;
	float result3 = depth <= s3 + SHADOW_EPSILON;

	// Transform to texel space.
	float2 texelPos = gShadowMapSize * projTexC.xy;

	// Determine the interpolation amounts.
	float2 t = frac(texelPos);

	// Interpolate results.
	return lerp(lerp(result0, result1, t.x),
				lerp(result2, result3, t.x), t.y);
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
	float specularIntensity = gColorMap.Sample( gColorSampler, input.TexC ).a;

	// For specular lighting, we need to have the vector from the camera to the
	// point being shaded, alas, we need the position. Right now we have the
	// depth in gDepthMap, which can be used to construct view space position.

	// Read depth
	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;
	float linearDepth = gProjB / (depth - gProjA);
	float3 posVS = input.ViewRay * linearDepth;

	// New for shadow mapping
	float4 posWS = mul(float4(posVS, 1.0f), gInvView);
	float4 shadowMapCoords = mul(posWS, gLightViewVolume);
	float shadowFactor = CalcShadowFactor(shadowMapCoords);

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
	float3 reflectionVector = normalize(reflect(lightVector, normal));

	// Camera-to-surface vector (camera position is origin because of view space :) )
	float3 directionToCamera = normalize(-posVS.xyz);

	// Compute specular light
	float specularLight = specularIntensity * pow(saturate(dot(reflectionVector,
		directionToCamera)), specularPower);

	// Output the two lights
	return float4(diffuseLight.rgb * shadowFactor, specularLight * shadowFactor);
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