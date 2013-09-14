float3 gDirectionVS;
float gAngleCosine;
float gDecayExponent;
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
	float3 PosV : POSITION;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul(float4(input.PosL, 1.0f), gWVP);
	output.PosV = mul(float4(input.PosL, 1.0f), gWorldView).xyz;

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Clamp view ray to the plane at Z = 1
	float3 viewRay = float3(input.PosV.xy / input.PosV.z, 1.0f);

	// Obtain texture coordinates corresponding to the current pixel
	// The screen coordinates are in [-1,1]x[1,-1]
	// The texture coordinates need to be in [0,1]x[0,1]
	float2 texCoord = float2(input.PosH.x / 1280.0f, input.PosH.y / 720.0f);

	// Get normal data from gNormalMap
	float4 normalData = gNormalMap.Sample( gNormalSampler, texCoord );
	// Transform normal back into [-1,1] range
	float3 normal = 2.0f * normalData.xyz - 1.0f;

	// Get specular power
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap
	float specularIntensity = gColorMap.Sample( gColorSampler, texCoord ).a;

	// Read depth
	float depth = gDepthMap.Sample( gDepthSampler, texCoord ).r;
	float linearDepth = gProjB / (depth - gProjA);
	float3 posVS = viewRay * linearDepth;

	// Surface-to-light vector
	float3 lightVector = gLightPositionVS - posVS;

	// Compute attenuation based on distance - linear attenuation
	float attenuation = saturate(1.0f - length(lightVector) / gLightRadius);

	// Normalize light vector
	lightVector = normalize(lightVector);

	// SpotDotLight = cosine of the angle between gDirection and lightVector
	float SdL = dot(gDirectionVS, -lightVector);
	if (SdL > gAngleCosine)
	{
		float spotIntensity = pow(max(SdL, 0.0f), gDecayExponent);

		// Compute diffuse light
		float NdL = max(0, dot(normal, lightVector));
		float3 diffuseLight = NdL * gLightColor.rgb;

		// Reflection vector
		float3 reflectionVector = normalize(reflect(-lightVector, normal));

		// Camera-to-surface vector (in VS camera position is origin)
		float3 directionToCamera = normalize(-posVS);

		// Compute specular light
		float specularLight = specularIntensity * pow(saturate(dot(reflectionVector,
			directionToCamera)), specularPower);

		// Take attenuation and light intensity into account. Don't forget spotIntensity!
		return attenuation * gLightIntensity * spotIntensity * float4(diffuseLight.rgb, specularLight);
	}

	return float4(0, 0, 0, 0);
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