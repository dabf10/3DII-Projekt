float4x4 gWVP;
float4x4 gWorldView;
float3 gLightColor;
float3 gLightPositionVS;
float gLightRadius; // How far the light reaches
float gLightIntensity = 1.0f; // Control the brightness of the light
float gProjA;
float gProjB;

Texture2D gColorMap; // Diffuse color, and specular intensity in alpha
Texture2D gNormalMap; // Normals, and specular power in alpha
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

struct VS_IN
{
	float3 PosL : POSITION;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float3 PosVS : POSITION;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul(float4(input.PosL, 1.0f), gWVP);
	output.PosVS = mul(float4(input.PosL, 1.0f), gWorldView).xyz;

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Clamp the view ray to the plane at Z = 1
	float3 viewRay = float3(input.PosVS.xy / input.PosVS.z, 1.0f);

	float2 texCoord = float2(input.PosH.x / 1280.0f, input.PosH.y / 720.0f);
	// Sample the depth and convert to linear view space Z (assume it gets
	// samples as a floating point value in the range [0,1])
	float depth = gDepthMap.Sample( gDepthSampler, texCoord ).r;
	float linearDepth = gProjB / (depth - gProjA);
	float3 posVS = viewRay * linearDepth;

	// Get normal data from gNormalMap
	float4 normalData = gNormalMap.Sample( gNormalSampler, texCoord );
	// Transform normal back into [-1,1] range
	float3 normal = 2.0f * normalData.xyz - 1.0f;

	// Get specular power
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap
	float4 color = gColorMap.Sample( gColorSampler, texCoord );
	float specularIntensity = color.a;

	// Surface-to-light vector
	float3 lightVector = gLightPositionVS - posVS;

	// Compute attenuation based on distance - linear attenuation
	float attenuation = saturate(1.0f - length(lightVector)/gLightRadius);

	// Normalize light vector
	lightVector = normalize(lightVector);

	// Compute diffuse light
	float NdL = max(0, dot(normal, lightVector));
	float3 diffuseLight = NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-lightVector, normal));

	// Camera-to-surface vector (in VS camera position is zero)
	float3 directionToCamera = normalize(-posVS.xyz);

	// Compute specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float y = specularPower;
	float specularLight = specularIntensity * pow(x, y);
	
	// Take attenuation and light intensity into account
	float3 ambientLight = float3( 0.3f, 0.3f, 0.3f );
	return float4( attenuation * gLightIntensity * color.rgb * (diffuseLight + ambientLight) + attenuation * gLightIntensity * specularLight, 1 );
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