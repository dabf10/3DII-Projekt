float4x4 gWVP;
float4x4 gWorldView;
float4x4 gProj;

float3 gDirectionVS;
float gCosOuter;
float gCosInner;
float3 gLightColor;
float3 gLightPositionVS;
float gLightRangeRcp;

Texture2D gColorMap; // Diffuse color, and specular intensity in alpha
Texture2D gNormalMap; // Normals, and specular power in alpha
Texture2D gDepthMap;

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
	float4 color = gColorMap.Load( uint3( input.PosH.xy, 0 ) );
	float specularIntensity = color.a;
	
	// Clamp view ray to the plane at Z = 1
	float3 viewRay = float3(input.PosV.xy / input.PosV.z, 1.0f);

	// Read depth
	float depth = gDepthMap.Load( uint3( input.PosH.xy, 0 ) ).r;
	float linearDepth = gProj[3][2] / (depth - gProj[2][2]);
	float3 posVS = viewRay * linearDepth;

	// ----------------------------------------------------

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
	float3 diffuseLight = NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-toLight, normal));

	// Camera-to-surface vector (in VS camera position is origin)
	float3 directionToCamera = normalize(-posVS);

	// Specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float y = specularPower;
	float specularLight = specularIntensity * pow(x, y);
	
	// Take attenuation and light intensity into account
	float3 ambientLight = float3( 0.3f, 0.3f, 0.3f );
	return float4( distAtt * coneAtt * color.rgb * (diffuseLight + ambientLight) + distAtt * coneAtt * specularLight, 1 );
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