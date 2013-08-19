float3 gDirection;
float gAngleCosine;
float gDecayExponent;
float4x4 gWorld;
float4x4 gView;
float4x4 gProjection;
float3 gLightColor;
float3 gCameraPosition; // Used for specular light
float4x4 gInvViewProj; // Used to compute world-pos
float3 gLightPosition;
float gLightRadius; // How far the light reaches
float gLightIntensity = 1.0f; // Control the brightness of the light

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
	float4 ScreenPos : TEXCOORD;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	float4 worldPos = mul(float4(input.PosL, 1.0f), gWorld);
	float4 viewPos = mul(worldPos, gView);
	output.PosH = mul(viewPos, gProjection);
	output.ScreenPos = output.PosH;

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Obtain screen position
	input.ScreenPos.xy /= input.ScreenPos.w;

	// Obtain texture coordinates corresponding to the current pixel
	// The screen coordinates are in [-1,1]x[1,-1]
	// The texture coordinates need to be in [0,1]x[0,1]
	float2 texCoord = 0.5f * (float2(input.ScreenPos.x, -input.ScreenPos.y) + 1);

	// Get normal data from gNormalMap
	float4 normalData = gNormalMap.Sample( gNormalSampler, texCoord );
	// Transform normal back into [-1,1] range
	float3 normal = 2.0f * normalData.xyz - 1.0f;

	// Get specular power
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap
	float specularIntensity = gColorMap.Sample( gColorSampler, texCoord ).a;

	// Read depth
	float depthVal = gDepthMap.Sample( gDepthSampler, texCoord ).r;

	// Compute screen-space position
	float4 position;
	position.xy = input.ScreenPos.xy;
	position.z = depthVal;
	position.w = 1.0f;

	// Transform to world space
	position = mul(position, gInvViewProj);
	position /= position.w;

	// Surface-to-light vector
	float3 lightVector = gLightPosition - position.xyz;

	// Compute attenuation based on distance - linear attenuation
	float attenuation = saturate(1.0f - length(lightVector) / gLightRadius);

	// Normalize light vector
	lightVector = normalize(lightVector);

	// SpotDotLight = cosine of the angle between gDirection and lightVector
	float SdL = dot(gDirection, -lightVector);
	if (SdL > gAngleCosine)
	{
		//float spotIntensity = pow(SdL, gDecayExponent);
		//float spotIntensity = pow(abs(SdL), gDecayExponent);
		float spotIntensity = pow(max(SdL, 0.0f), gDecayExponent);

		// Compute diffuse light
		float NdL = max(0, dot(normal, lightVector));
		float3 diffuseLight = NdL * gLightColor.rgb;

		// Reflection vector
		float3 reflectionVector = normalize(reflect(-lightVector, normal));

		// Camera-to-surface vector
		float3 directionToCamera = normalize(gCameraPosition - position.xyz);

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