float4x4 gInvProj; // For full screen triangle stuff.
float4x4 gProj;

float3 gLightPositionVS;
float gLightRangeRcp;
float3 gLightDirectionVS;
float gLightLength;
float3 gLightColor;
float gLightIntensity;

Texture2D gColorMap; // Diffuse color, and specular intensity in alpha
Texture2D gNormalMap; // Normals, and specular power in alpha
Texture2D gDepthMap;

struct GBuffer
{
	float3 Diffuse;
	float3 Normal;
	float3 PosVS;
	float SpecularIntensity;
	float SpecularPower;
};

// Full screen triangle at first try to get light up and running.
// Improve later with proper volume and change vertex shader accordingly.
struct VS_OUT
{
	float4 PosH : SV_Position;
	float3 PosV : POSITION;
};

VS_OUT VS( uint VertexID : SV_VertexID )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH.x = (VertexID == 2) ? 3.0f : -1.0f;
	output.PosH.y = (VertexID == 0) ? -3.0f : 1.0f;
	output.PosH.zw = 1.0f;

	output.PosV = mul(output.PosH, gInvProj).xyz;

	return output;
}

float3 EvaluateCapsuleLight( GBuffer gbuffer )
{
	float3 toCapsuleStart = gbuffer.PosVS - gLightPositionVS;

	// Project start-to-fragment onto light direction to get distance from
	// light position to closest point on the line (dot product). If this value
	// is negative, we are outside the line from the start point side, which means
	// that the start point is the closest point. If it's greater than the light
	// length (outside line from end point side), the closest point is the end
	// point. Otherwise it's on the line. The value is normalized by dividing
	// by the light length, followed by saturation to clamp in [0,1]. Multiplying
	// this normalized value with the light length, we get correct distance from
	// start point, taking end points into consideration :)
	float distOnLine = dot( toCapsuleStart, gLightDirectionVS ) / gLightLength;
	distOnLine = saturate(distOnLine) * gLightLength;
	float3 pointOnLine = gLightPositionVS + gLightDirectionVS * distOnLine;
	float3 toLight = pointOnLine - gbuffer.PosVS;
	float distToLight = length(toLight);

	// Diffuse light
	toLight /= distToLight; // Normalize
	float NdL = saturate(dot(toLight, gbuffer.Normal));
	float3 diffuseLight = gLightIntensity * NdL * gLightColor;

	float3 reflectionVector = normalize(reflect(-toLight, gbuffer.Normal));

	// Camera-to-surface vector (in VS camera position is zero)
	float3 directionToCamera = normalize(-gbuffer.PosVS);

	// Specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);

	// Linear distance attenuation
	float attenuation = saturate(1.0f - distToLight * gLightRangeRcp);

	return attenuation * (diffuseLight + specularLight);
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	GBuffer gbuffer;

	float depth = gDepthMap.Load( uint3( input.PosH.xy, 0 ) ).r;
	float4 diffuse_specIntensity = gColorMap.Load( uint3( input.PosH.xy, 0 ) );
	float4 normal_specPower = gNormalMap.Load( uint3( input.PosH.xy, 0 ) );

	// Reconstruct view space position
	// Clamp view ray to the plane at Z = 1
	float3 viewRay = float3(input.PosV.xy / input.PosV.z, 1.0f);
	float linearDepth = gProj[3][2] / (depth - gProj[2][2]);
	float3 posVS = viewRay * linearDepth;

	gbuffer.PosVS = posVS;
	gbuffer.Diffuse = diffuse_specIntensity.rgb;
	gbuffer.Normal = normalize(2.0f * normal_specPower.xyz - 1.0f); // Transform back into [-1,1] range
	gbuffer.SpecularIntensity = diffuse_specIntensity.a;
	gbuffer.SpecularPower = normal_specPower.a * 255;

	// ------------------------------------------------

	return float4( gbuffer.Diffuse * EvaluateCapsuleLight( gbuffer ), 1 );
}

technique11 Technique0
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS() ) ) ;
	}
}