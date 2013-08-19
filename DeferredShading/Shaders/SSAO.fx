// Vad som kan funderas över/göras annorlunda.
// För alla samples offsettar man inte normalen i world/view space utan
// snarare i texture space och använder de nya texturkoordinaterna för att
// sampla depth map. Det gör att djupvärdena i världen är beroende av var
// på skärmen dess punkter projiceras. Vidare innebär det också att en viss
// yta på skärmen motsvarar större volym i världen om man är långt borta.
// Är man väldigt nära samplar man bara en liten volym. Alltså kan man
// i ett visst läge få ett resultat som ser bra ut, men zoomar man in kan
// AO helt försvinna. Det hade varit bättre att sampla i världen genom att
// offsetta position. Då måste man projicera tillbaka till screen space för
// att hitta närmsta occluder vilket kommer bli dyrare, men det är nog så
// man får göra, eftersom att offsetta i screen space inte ger konsistent resultat.

Texture2D gNormalMap;
Texture2D gDepthMap;

SamplerState gNormalSampler
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

float4x4 gInvViewProjection; // TODO: Remove me, this is to test in world space. Replace with gInvProjection

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

float3 CalculatePosition( float2 texC, float depth )
{
	float x = texC.x * 2.0f - 1.0f;
	float y = -(texC.y * 2.0f - 1.0f);
	float4 projPos = float4(x, y, depth, 1.0f);
	// Transform projected position to view-space position using inverse projection matrix.
	//float4 posV = mul(projPos, gInvProjection);
	float4 posV = mul(projPos, gInvViewProjection); // TODO: Remove this. Not really view pos, but transforms to world for testing purposes
	// Divide by w to get the view-space position.
	return posV.xyz / posV.w;
}

// ============================================================================
// Hemispherical SSAO using Poisson-Disk sampling in screen-space.
// ============================================================================

//float4x4 gInvView; // TODO: Remove this line
//float gDistanceThreshold;
//float2 gFilterRadius;
//
//static const int gSampleCount = 16;
//static const float2 gPoisson16[] = { // These are the Poisson Disk Samples
//	float2( -0.94201624, -0.39906216 ),
//	float2(  0.94558609, -0.76890725 ),
//	float2( -0.09418410, -0.92938870 ),
//	float2(  0.34495938,  0.29387760 ),
//	float2( -0.91588581,  0.45771432 ),
//	float2( -0.81544232, -0.87912464 ),
//	float2( -0.38277543,  0.27676845 ),
//	float2(  0.97484398,  0.75648379 ),
//	float2(  0.44323325, -0.97511554 ),
//	float2(  0.53742981, -0.47373420 ),
//	float2( -0.26496911, -0.41893023 ),
//	float2(  0.79197514,  0.19090188 ),
//	float2( -0.24188840,  0.99706507 ),
//	float2( -0.81409955,  0.91437590 ),
//	float2(  0.19984126,  0.78641367 ),
//	float2(  0.14383161, -0.14100790 )
//};
//
//float4 PS( VS_OUT input ) : SV_TARGET
//{
//	// Reconstruct position from depth
//	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;
//	float3 posV = CalculatePosition( input.TexC, depth );
//
//	// Get the view space normal and transform into [-1,1] range
//	float3 normalV = gNormalMap.Sample( gNormalSampler, input.TexC ).xyz * 2.0f - 1.0f;
//	normalV = mul(normalV, (float3x3)gInvView); // TODO: Remove this line, not needed
//
//	float ambientOcclusion = 0.0f;
//	// Perform AO
//	for (int i = 0; i < gSampleCount; ++i)
//	{
//		// TODO: Här borde man nog offsetta i världen och sedan projicera tillbaka
//		// till screen space för att få vilken texturkoordinat som ska samplas.
//		// Därefter kan man nog fortsätta som vanligt.
//		// Sample at an offset specified by the current Poisson-Disk sample and
//		// scale it by a radius (has to be in texture-space).
//		float2 sampleTexCoord = input.TexC + (gPoisson16[i] * gFilterRadius);
//		float sampleDepth = gDepthMap.Sample( gDepthSampler, sampleTexCoord ).r;
//		// TODO: Varför * 2 - 1 på depth men inte tidigare? Tänk på original är GLSL
//		//float3 samplePos = CalculatePosition( sampleTexCoord, sampleDepth * 2 - 1 );
//		float3 samplePos = CalculatePosition( sampleTexCoord, sampleDepth );
//		float3 sampleDir = normalize(samplePos - posV);
//
//		// Angle between surface normal and sample direction
//		float NdS = max(dot(normalV, sampleDir), 0);
//		// Distance between surface position and sample position
//		float VPdistSP = distance(posV, samplePos);
//
//		// a = distance function
//		float a = 1.0f - smoothstep(gDistanceThreshold, gDistanceThreshold * 2, VPdistSP);
//		// b = dot product
//		float b = NdS;
//
//		ambientOcclusion += (a * b);
//	}
//
//	float ambientAccessibility = 1.0f - (ambientOcclusion / gSampleCount );
//
//	return float4(ambientAccessibility, ambientAccessibility, ambientAccessibility, 1.0f);
//}

// ============================================================================
// My implementation
// ============================================================================

Texture2D gRandomNormals;

SamplerState gRandomNormalSampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = WRAP;
	AddressV = WRAP;
};

float4x4 gProjection;
float gAOStart;
float gHemisphereRadius;
float4x4 gView; // to remove

float gOffset;

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

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Reconstruct position from depth
	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;

	// Early exit if max depth, this can probably be removed if using a skymap.
	clip( depth == 1.0f ? -1 : 1 );

	float3 posVS = CalculatePosition( input.TexC, depth );

	// TODO: The normals are actually stored in world space right now.
	// TODO: Normalize?
	// Get the view space normal and transform into [-1,1] range
	float3 normalVS = gNormalMap.Sample( gNormalSampler, input.TexC ).xyz * 2.0f - 1.0f;
	normalVS = normalize(mul(normalVS, (float3x3)gView));

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

		// Get the depth of the occluder fragment and use it to find it's position.
		float occluderDepth = gDepthMap.Sample( gDepthSampler, occluderTexC ).r;
		float3 occluderPos = CalculatePosition( occluderTexC, occluderDepth );

		float3 directionToOccluder = normalize(occluderPos - posVS);

		// Angle between surface normal and occluder direction
		float NdS = max(dot(normalVS, directionToOccluder), 0);

		// Distance between surface position and occluder position
		float VPdistOP = distance(posVS, occluderPos);

		// a = distance function (smoothstep value could be raised to a power for exponential falloff instead of linear, smoothstep is in [0,1] which means smoothstep^n is in [0,1] as well.
		float a = 1.0f - smoothstep(gAOStart, gHemisphereRadius, VPdistOP);
		// b = dot product
		float b = NdS;

		ambientOcclusion += (a * b);
	}

	// Output the result
	float ambientAccessibility = 1.0f - ambientOcclusion * gInvSamples;

	return float4(ambientAccessibility, ambientAccessibility, ambientAccessibility, 1.0f);
}

// ============================================================================
// Hemispherical SSAO using noise map from gamerendering.com
// ============================================================================

//Texture2D gRandomNormals;
//
//SamplerState gRandomNormalSampler
//{
//	Filter = MIN_MAG_MIP_POINT;
//	AddressU = WRAP;
//	AddressV = WRAP;
//};
//
//float4x4 gView; // to remove
//float gTotStrength;
//float gStrength;
//float gOffset;
//float gFalloff;
//float gRad;
//float gSamples = 16;
//static const float gInvSamples = 1.0f / 16;
//static const float3 gRandomSphereVectors[] = { // Random vectors inside unit sphere
//	float3(  0.53812504,  0.18565957, -0.43192000 ),
//	float3(  0.13790712,  0.24864247,  0.44301823 ),
//	float3(  0.33715037,  0.56794053, -0.00578950 ),
//	float3( -0.69998050, -0.04511441, -0.00199656 ),
//	float3(  0.06896307, -0.15983082, -0.85477847 ),
//	float3(  0.05609944,  0.00695497, -0.18433520 ),
//	float3( -0.01465364,  0.14027752,  0.07620370 ),
//	float3(  0.01001993, -0.19242250, -0.03444339 ),
//	float3( -0.35775623, -0.53019690, -0.43581226 ),
//	float3( -0.31692210,  0.10636073,  0.01586092 ),
//	float3(  0.01035035, -0.58698344,  0.00462939 ),
//	float3( -0.08972908, -0.49408212,  0.32879040 ),
//	float3(  0.71199860, -0.01546900, -0.09183723 ),
//	float3( -0.05338235,  0.05967581, -0.54118990 ),
//	float3(  0.03526766, -0.06318861,  0.54602677 ),
//	float3( -0.47761092,  0.28479110, -0.02717160 )
//};
//
//float4 PS( VS_OUT input ) : SV_TARGET
//{
//	// Grab a normal for reflecting the sample rays later on
//	float3 randomVector = normalize((gRandomNormals.Sample( gRandomNormalSampler, input.TexC * gOffset ).xyz * 2.0f) - 1.0f);
//	//randomVector = mul(randomVector, (float3x3)gView);
//
//	// Get current pixel depth
//	float depth = gDepthMap.Sample( gDepthSampler, input.TexC ).r;
//	depth = CalculatePosition( input.TexC, depth ).z / (1000.0f - 0.1f);
//
//	// Current fragment coords in screen-space
//	float3 ep = float3(input.TexC, depth);
//
//	// Get the normal of the current pixel
//	float3 normal = normalize(gNormalMap.Sample( gNormalSampler, input.TexC ).xyz * 2.0f - 1.0f);
//	normal = mul(normal, (float3x3)gView);
//
//	float bl = 0.0f;
//	// Adjust for the depth (not sure if this is good...)
//	float radD = gRad/depth;
//
//	float3 ray, se, occNorm;
//	float occluderDepth, depthDifference, normDiff;
//
//	for (int i = 0; i < gSamples; ++i)
//	{
//		// Get a vector (randomized inside of a sphere with radius 1.0f) from a
//		// texture and reflect it
//		ray = radD * reflect(gRandomSphereVectors[i], randomVector);
//
//		// If the ray is outside the hemisphere, then change direction
//		se = ep + sign(dot(ray, normal)) * ray;
//
//		// Get the depth of the occluder fragment
//		occluderDepth = gDepthMap.Sample( gDepthSampler, se.xy ).r;
//		occluderDepth = CalculatePosition( input.TexC, occluderDepth ).z / (1000.0f - 0.1f);
//
//		// Get the normal of the occluder fragment
//		occNorm = normalize(gNormalMap.Sample( gNormalSampler, se.xy ).xyz * 2.0f - 1.0f);
//		occNorm = mul(occNorm, (float3x3)gView);
//
//		// If depth difference is negative: occluder is behind current fragment
//		depthDifference = depth - occluderDepth;
//
//		// Calculate the difference between the normals as a weight
//		normDiff = (1.0f - dot(occNorm, normal));
//		// The falloff equation, starts at falloff and is kind of 1/x^2 falling
//		bl += step(gFalloff, depthDifference) * normDiff * (1.0f - smoothstep(gFalloff, gStrength, depthDifference));
//	}
//
//	// Output the result
//	float ao = 1.0f - gTotStrength * bl * gInvSamples;
//
//	return float4(ao, ao, ao, 1.0f);
//}

technique11 Technique0
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, VS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PS() ) );
	}
}