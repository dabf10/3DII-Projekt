float4x4 gLightViewProj;
float4x4 gWVP;
float4x4 gWorldView;
float4x4 gProj;

float3 gDirectionVS;
float gCosOuter;
float gCosInner;
float3 gLightPositionVS;
float gLightRangeRcp;
float gLightIntensity;

Texture2D gProjLightTex : register( t0 );

Texture2D gColorMap; // Diffuse color, and specular intensity in alpha
Texture2D gNormalMap; // Normals, and specular power in alpha
Texture2D gDepthMap;

SamplerState gSamLinear
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = WRAP;
	AddressV = WRAP;
};

// Important: If position is in VS, gLightViewProj needs to be
// view -> world -> light space -> light projection. In other words,
// gLightViewProj must correctly transform a position to light proj space.
float2 GetProjPos( float4 position )
{
	// Project position. We don't care about depth, which is why z is skipped.
	// w is saved however, so that we can divide xy by it to finish perspective
	// projection (this is automatically done on SV_Position between VS and PS).
	float3 projTexXYW = mul( position, gLightViewProj ).xyw;
	projTexXYW.xy /= projTexXYW.z; // Perspective correction

	// Transform from [-1,1] to [0,1]
	float2 UV = (projTexXYW.xy + 1.0.xx) * float2(0.5, -0.5);
	return UV;
}

float3 GetLightColor( float2 uv )
{
	return gLightIntensity * gProjLightTex.Sample( gSamLinear, uv ).xyz;
}

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
	// Get normal data from gNormalMap
	float4 normalData = gNormalMap.Load( uint3( input.PosH.xy, 0 ) );
	// Transform normal back into [-1,1] range
	float3 normal = normalize(2.0f * normalData.xyz - 1.0f);

	// Get specular power
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap
	float specularIntensity = gColorMap.Load( uint3( input.PosH.xy, 0 ) ).a;
	
	// Clamp view ray to the plane at Z = 1
	float3 viewRay = float3(input.PosV.xy / input.PosV.z, 1.0f);

	// Read depth
	float depth = gDepthMap.Load( uint3( input.PosH.xy, 0 ) ).r;
	float linearDepth = gProj[3][2] / (depth - gProj[2][2]);
	float3 posVS = viewRay * linearDepth;

	// ----------------------------------------------------

	float2 uv = GetProjPos(float4(posVS, 1));
	float3 lightColor = GetLightColor(uv);

	float3 toLight = gLightPositionVS - posVS;
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
	float NdL = saturate( dot( normal, toLight ) );
	float3 diffuseLight = NdL * lightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-toLight, normal));

	// Camera-to-surface vector (in VS camera position is origin)
	float3 directionToCamera = normalize(-posVS);

	// Specular light
	float specularLight = specularIntensity * pow(saturate(dot(reflectionVector,
		directionToCamera)), specularPower);
	
	return distAtt * coneAtt * float4(diffuseLight.rgb, specularLight);
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