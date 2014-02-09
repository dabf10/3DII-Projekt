Texture2D gNormalMap;
Texture2D gDepthMap;
Texture2D gRandomNormals;

SamplerState gDepthSampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gRandomNormalSampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = WRAP;
	AddressV = WRAP;
};

float4x4 gProjection;
float4x4 gInvProj;
float gAOStart;
float gHemisphereRadius;
float gOffset; // Used to alter texture coordinates when sampling random normals.
float gSamples = 16;
static const float gInvSamples = 1.0f / 16;
static const float3 gRandomSphereVectors[] = { // Random vectors inside unit sphere
	float3(  0.53812504,  0.18565957, -0.43192000 ),
	float3(  0.13790712,  0.24864247,  0.44301823 ),
	float3(  0.33715037,  0.56794053, -0.00578950 ),
	float3( -0.69998050, -0.04511441, -0.00199656 ),
	float3(  0.06896307, -0.15983082, -0.85477847 ),
	float3(  0.05609944,  0.00695497, -0.18433520 ),
	float3( -0.01465364,  0.14027752,  0.07620370 ),
	float3(  0.01001993, -0.19242250, -0.03444339 ),
	float3( -0.35775623, -0.53019690, -0.43581226 ),
	float3( -0.31692210,  0.10636073,  0.01586092 ),
	float3(  0.01035035, -0.58698344,  0.00462939 ),
	float3( -0.08972908, -0.49408212,  0.32879040 ),
	float3(  0.71199860, -0.01546900, -0.09183723 ),
	float3( -0.05338235,  0.05967581, -0.54118990 ),
	float3(  0.03526766, -0.06318861,  0.54602677 ),
	float3( -0.47761092,  0.28479110, -0.02717160 )
};

struct VS_OUT
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
	float3 ViewRay : POSITION;
};

VS_OUT VS( uint VertexID : SV_VertexID )
{
	VS_OUT output = (VS_OUT)0;

	output.PosH.x = (VertexID == 2) ? 3.0f : -1.0f;
	output.PosH.y = (VertexID == 0) ? -3.0f : 1.0f;
	output.PosH.zw = 1.0f;

	output.TexC = output.PosH.xy * float2(0.5f, -0.5f) + 0.5f;

	float3 posVS = mul(output.PosH, gInvProj).xyz;
	output.ViewRay = float3(posVS.xy / posVS.z, 1.0f);

	return output;
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;

	// Early exit if max depth, this can probably be removed if using a skymap.
	clip( depth == 1.0f ? -1 : 1 );

	float linearDepth = gProjection[3][2] / (depth - gProjection[2][2]);
	float3 posVS = input.ViewRay * linearDepth;

	// Get the view space normal and transform into [-1,1] range
	float3 normalVS = normalize( gNormalMap.Load( uint3( 2 * input.PosH.xy, 0 ) ).xyz * 2.0f - 1.0f);

	// Grab a normal for reflecting the sample rays later on. There are fixed
	// vectors that are always the same (one for each sample). These vectors
	// will be reflected in this random vector, effectively granting us random
	// vectors for every sample, using only a single texture fetch.
	float3 randomVector = normalize((gRandomNormals.Sample( gRandomNormalSampler, input.TexC * gOffset ).xyz * 2.0f) - 1.0f);

	float ambientOcclusion = 0.0f;

	// Loop through every sample we want to take.
	for (int i = 0; i < gSamples; ++i)
	{
		// TODO: The view space position will be offset by this ray (which is
		// inside the hemisphere) to find a possible new occluder. Because of that
		// we might want to normalize the ray and multiply it by a hemisphere radius.
		// This is likely ok because the new points will be projected to find the
		// texcoords to find closest depth with. However, it would probably be
		// better to get sample points inside the hemisphere, not just on the surface.

		// TODO: Half the samples could be taken on the hemisphere surface
		// (offset position by normalized sampleRay * gHemisphereRadius).
		// Look up if the reflected vector (sampleRay) is indeed of unit length
		// if the reflectand is unit length. Check math on msdn for reflect and
		// verify that it is the case. For the other half, we multiply sampleRay
		// with 0.5f * gHemisphereRadius. If the sample rays are evenly distributed
		// this just might give a good result. Reflecting half the number of
		// samples and offsetting position by half-radius and radius in the
		// direction of the reflected vector will take two samples along that
		// direction: on surface and between surface and point. This would save
		// gSamples / 2 number of reflect-statements, but the result might not
		// be good enough for the little performance saved (probably very fast math).

		// Grab this sample´s sphere vector and reflect it in the random vector
		// fetched earlier. This results in a random vector for this sample.
		float3 sampleRay = reflect(gRandomSphereVectors[i], randomVector);

		// If the ray is outside the hemisphere, then change direction. Offset
		// the position using the final ray.
		float3 offsetPos = posVS + sign(dot(sampleRay, normalVS)) * sampleRay;

		// Project the offset position in order to find tex coords for occluder.
		float4 projOffsetPos = mul(float4(offsetPos, 1.0f), gProjection);
		projOffsetPos /= projOffsetPos.w;

		// Use the x and y values of the projected offset position to find what
		// tex coords to use to sample occluder depth and normal. X and Y of the
		// projected offset position are in [-1,1]. We want to transform them
		// into texture space, which is [0,1]. Negate Y-coordinate because in
		// clip space it points upwards, in texture space downwards.
		float2 occluderTexC;
		occluderTexC.x = (projOffsetPos.x + 1.0f) * 0.5f;
		occluderTexC.y = (-projOffsetPos.y + 1.0f) * 0.5f;

		float3 occluderViewRay = float3(offsetPos.xy / offsetPos.z, 1.0f);

		// Get the depth of the occluder fragment and use it to find it's position.
		float occluderDepth = gDepthMap.Sample( gDepthSampler, occluderTexC ).r;
		float linearOccluderDepth = gProjection[3][2] / (occluderDepth - gProjection[2][2]);
		float3 occluderPos = occluderViewRay * linearOccluderDepth;

		float3 directionToOccluder = normalize(occluderPos - posVS);

		// Angle between surface normal and occluder direction
		float NdS = max(dot(normalVS, directionToOccluder), 0);

		// Distance between surface position and occluder position
		float VPdistOP = distance(posVS, occluderPos);

		// a = distance function (smoothstep value could be raised to a power for exponential falloff instead of linear, smoothstep is in [0,1] which means smoothstep^n is in [0,1] as well.
		float a = 1.0f - smoothstep(gAOStart, gHemisphereRadius, VPdistOP);
		// b = dot product
		float b = NdS; // Angle attenuation (normal and direction to occluder)

		ambientOcclusion += (a * b);
	}

	// Output the result
	float ambientAccessibility = 1.0f - ambientOcclusion * gInvSamples;
	
	return float4(ambientAccessibility, 0, 0, 0);
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