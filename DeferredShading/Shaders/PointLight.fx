float4x4 gWVP;
float4x4 gWorldView;
float4x4 gProj;

float3 gLightColor;
float3 gLightPositionVS;
float gLightRadius; // How far the light reaches
float gLightIntensity; // Control the brightness of the light

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

float3 EvaluatePointLight( GBuffer gbuffer )
{
	// Surface-to-light vector
	float3 lightVector = gLightPositionVS - gbuffer.PosVS;

	// Compute attenuation based on distance - linear attenuation
	float distToLight = length(lightVector);
	float attenuation = saturate(1.0f - distToLight / gLightRadius);

	// Normalize light vector
	lightVector /= distToLight;

	// Compute diffuse light
	float NdL = max(0, dot(gbuffer.Normal, lightVector));
	float3 diffuseLight = NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-lightVector, gbuffer.Normal));

	// Camera-to-surface vector (in VS camera position is zero)
	float3 directionToCamera = normalize(-gbuffer.PosVS);

	// Compute specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);
	
	// Take attenuation and light intensity into account
	return attenuation * gLightIntensity * (diffuseLight + specularLight);
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

	// -------------------------------------

	return float4( gbuffer.Diffuse * EvaluatePointLight( gbuffer ), 1 );
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