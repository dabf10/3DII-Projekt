float4x4 gLightTransform; // Inverse world
float4x4 gWVP;
float4x4 gWorldView;
float4x4 gWorld;
float3 gLightPositionVS;
float gLightRadius;
float gLightIntensity = 1.0f;
float gProjA;
float gProjB;

Texture2D gColorMap;
Texture2D gNormalMap;
Texture2D gDepthMap;

TextureCube gProjLightTex : register( t0 );

SamplerState gSamPoint
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gSamLinearCube
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = WRAP;
	AddressV = WRAP;
	AddressW = WRAP;
	MaxAnisotropy = 1;
	ComparisonFunc = ALWAYS;
	MaxLOD = ~0;
};

struct VS_IN
{
	float3 PosL : POSITION;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float3 PosVS : POSITION0;
	float3 PosW : POSITION1;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = mul( float4(input.PosL, 1.0f), gWVP );
	output.PosVS = mul( float4(input.PosL, 1.0f), gWorldView ).xyz;
	output.PosW = mul( float4(input.PosL, 1.0f), gWorld ).xyz;

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Clamp the view ray to the plane at Z = 1
	float3 viewRay = float3(input.PosVS.xy / input.PosVS.z, 1.0f);

	float2 texCoord = float2(input.PosH.x / 1280.0f, input.PosH.y / 720.0f);
	// Sample the depth and convert to linear view space Z (assume it gets
	// samples as a floating point value in the range [0,1])
	float depth = gDepthMap.Sample( gSamPoint, texCoord ).r;
	float linearDepth = gProjB / (depth - gProjA);
	float3 posVS = viewRay * linearDepth;

	// Get normal data from gNormalMap
	float4 normalData = gNormalMap.Sample( gSamPoint, texCoord );
	// Transform normal back into [-1,1] range
	float3 normal = 2.0f * normalData.xyz - 1.0f;

	// Get specular power
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap
	float specularIntensity = gColorMap.Sample( gSamPoint, texCoord ).a;

	// Surface-to-light vector
	float3 lightVector = gLightPositionVS - posVS;

	// Compute attenuation based on distance - linear attenuation
	float attenuation = saturate(1.0f - length(lightVector)/gLightRadius);

	// Normalize light vector
	lightVector = normalize(lightVector);

	// TODO: Troligtvis fel i beräkning av sampledirection
	//float3 sampleDirection = gLightTransform[3].xyz + input.PosW;
	//sampleDirection = mul( sampleDirection.xyz, (float3x3)gLightTransform );
	float3 sampleDirection = gLightPositionVS - posVS;
	sampleDirection = mul( sampleDirection.xyz, (float3x3)gLightTransform );

	float3 lightColor = gLightIntensity * gProjLightTex.Sample( gSamLinearCube, sampleDirection ).rgb;

	// Compute diffuse light
	float NdL = max(0, dot(normal, lightVector));
	float3 diffuseLight = NdL * lightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-lightVector, normal));

	// Camera-to-surface vector (in VS camera position is zero)
	float3 directionToCamera = normalize(-posVS.xyz);

	// Compute specular light
	float specularLight = specularIntensity * pow(saturate(dot(reflectionVector,
		directionToCamera)), specularPower);

	// Take attenuation and light intensity into account
	return attenuation * gLightIntensity * float4(diffuseLight.rgb, specularLight);
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

//float3 GetDirToLight( float3 worldPosition )
//{
//	float3 toLight = gLightTransform[3].xyz + worldPosition;
//	return mul( toLight.xyz, (float3x3)gLightTransform );
//}
//
//float3 GetLightColor( float3 sampleDirection )
//{
//	return gPointIntensity * gProjLightTex.Sample( gLinearCubeSam, sampleDirection );
//}
//
//float3 CalcPoint( float3 lightColor, float3 position, Material material )
//{
//	float3 ToLight = PointLightPos - position;
//	float3 ToEye = EyePosition.xyz - position;
//	float DistToLight = length(ToLight);
//
//	// Phong diffuse
//	ToLight /= DistToLight; // Normalize
//	float NDotL = saturate(dot(ToLight, material.normal));
//	float3 finalColor = LightColor * NDotL;
//
//	// Blinn specular
//	ToEye = normalize(ToEye);
//	float3 HalfWay = normalize(ToEye + ToLight);
//	float NDotH = saturate(dot(HalfWay, material.normal));
//	finalColor += LightColor * pow(NDotH, material.specExp) * material.specIntensity;
//
//	// Attenuation
//	float DistToLightNorm = 1.0 - saturate(DistToLight * PointLightRangeRcp);
//	float Attn = DistToLightNorm * DistToLightNorm;
//	finalColor *= material.diffuseColor * Attn;
//
//	return finalColor;
//}