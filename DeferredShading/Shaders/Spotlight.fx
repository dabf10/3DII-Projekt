float4x4 gWVP;
float4x4 gWorldView;
float4x4 gProj;

float3 gDirectionVS;
float gCosOuter;
float gCosInner;
float3 gLightColor;
float3 gLightPositionVS;
float gLightRangeRcp;
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

struct VS_IN
{
	float3 PosL : POSITION;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float3 PosV : POSITION;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul(float4(input.PosL, 1.0f), gWVP);
	output.PosV = mul(float4(input.PosL, 1.0f), gWorldView).xyz;

	return output;
}

float3 EvaluateSpotLight( GBuffer gbuffer )
{
	float3 toLight = gLightPositionVS - gbuffer.PosVS;
	float distToLight = length(toLight);
	toLight /= distToLight; // Normalize

	// Linear distance attenuation
	float distAtt = saturate(1.0f - distToLight * gLightRangeRcp);
	
	// Cone attenuation
	// Angle between lightvector and spot direction (dot) within inner cone: Full
	// attenuation. Outside outer cone: zero attenuation. Between: decrease from 
	// 1 to 0.
	float coneAtt = smoothstep( gCosOuter, gCosInner, dot( gDirectionVS, -toLight ) );
	
	// Diffuse light
	float NdL = saturate( dot( gbuffer.Normal, toLight ) );
	float3 diffuseLight = gLightIntensity * NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-toLight, gbuffer.Normal));

	// Camera-to-surface vector (in VS camera position is origin)
	float3 directionToCamera = normalize(-gbuffer.PosVS);

	// Specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);
	
	// Take attenuation and light intensity into account
	return distAtt * coneAtt * (diffuseLight + specularLight);
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

	// ----------------------------------------------------

	return float4( gbuffer.Diffuse * EvaluateSpotLight( gbuffer ), 1 );
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