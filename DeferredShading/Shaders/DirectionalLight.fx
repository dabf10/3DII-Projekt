// Output the diffuse light (which may be colored), to the rgb channels, and
// the specular light (which will always be considered white), to the alpha channel.
// When combining these in the end, we will use the equation
//			FinalColor = DiffuseColor * DiffuseLight + SpecularLight

// Property of directional light
float3 gLightDirection;

// Property of directional light
float3 gLightColor;

// Position of the camera, used for specular light calculation
float3 gCameraPosition;

// In order to compute the world position of a pixel when knowing the screen
// depth, the inverse of the ViewProjection matrix is needed.
float4x4 gInvViewProj;

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

struct VS_IN
{
	float3 PosH : POSITION;
	float2 TexC : TEXCOORD;
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

VS_OUT VS( VS_IN input )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH = float4(input.PosH, 1.0f);
	output.TexC = input.TexC;

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Get the data we need out of the G-Buffer.

	// Normal data
	float4 normalData = gNormalMap.Sample( gNormalSampler, input.TexC );
	// Transform normal back into [-1,1] range
	float3 normal = 2.0f * normalData.xyz - 1.0f;
	// Get specular power, and transform into [0,255] range
	float specularPower = normalData.a * 255;

	// Get specular intensity from gColorMap.
	float specularIntensity = gColorMap.Sample( gColorSampler, input.TexC ).a;

	// For specular lighting, we need to have the vector from the camera to the
	// point being shaded, alas, we need the position. Right now we have the
	// depth in gDepthMap, and position on the screen in the [0,1]x[0,1] range,
	// which comes from the texture coordinates. This will transformed into
	// screen coordinates, which are in the [-1,1]x[-1,1] range, and then using
	// the gInvViewProj matrix, we get back into world coordinates.

	// Read depth
	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;

	// Compute screen-space position
	float4 position;
	position.x = input.TexC.x * 2.0f - 1.0f;
	position.y = -(input.TexC.y * 2.0f - 1.0f);
	position.z = depth;
	position.w = 1.0f;

	// Transform to world space
	position = mul(position, gInvViewProj);
	position /= position.w;

	// After we compute the vector from the surface to the light (which in this
	// case is the negated gLightDirection), we compute the diffuse light with
	// the dot product between the normal and the light vector. The specular light
	// is computed using the dot product between the light reflection vector and
	// the camera-to-object vector. The output will contain the diffuse light in
	// the RGB channels, and the specular light in the A channel.

	// Surface-to-light
	float3 lightVector = -normalize(gLightDirection);

	// Compute diffuse light
	float NdL = max(0, dot(normal, lightVector));

	float3 diffuseLight = NdL * gLightColor.rgb;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(lightVector, normal));

	// Camera-to-surface vector
	float3 directionToCamera = normalize(gCameraPosition - position.xyz);

	// Compute specular light
	float specularLight = specularIntensity * pow(saturate(dot(reflectionVector,
		directionToCamera)), specularPower);

	// Output the two lights
	return float4(diffuseLight.rgb, specularLight);
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