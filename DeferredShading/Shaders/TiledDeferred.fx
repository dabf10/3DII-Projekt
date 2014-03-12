// Input
Texture2D<float4> gColorMap : register( t0 );
Texture2D<float4> gNormalMap : register( t1 );
Texture2D<float4> gDepthMap : register( t2 );

// Output
RWTexture2D<float4> gOutputTexture : register( u0 ); // Full ihopsatt och ljussatt HDR textur

struct GBuffer
{
	float3 Diffuse;
	float3 Normal;
	float3 PosVS;
	float SpecularIntensity;
	float SpecularPower;
};

struct PointLight
{
	float3 PositionVS;
	float Radius;
	float3 Color;
	float Intensity;
};

struct SpotLight
{
	float3 DirectionVS;
	float CosOuter;
	float CosInner;
	float3 Color;
	float3 PositionVS;
	float RangeRcp;
	float Intensity;
};

struct CapsuleLight
{
	float3 PositionVS;
	float RangeRcp;
	float3 DirectionVS;
	float Length;
	float3 Color;
	float Intensity;
};

int gPointLightCount;
int gSpotLightCount;
int gCapsuleLightCount;
StructuredBuffer<PointLight> gPointLights;
StructuredBuffer<SpotLight> gSpotLights;
StructuredBuffer<CapsuleLight> gCapsuleLights;

float4x4 gProj;
float4x4 gInvProj;
float gBackbufferWidth;
float gBackbufferHeight;

groupshared uint minDepth;
groupshared uint maxDepth;

groupshared uint visiblePointLightCount;
groupshared uint visiblePointLightIndices[1024];
groupshared uint visibleSpotLightCount;
groupshared uint visibleSpotLightIndices[1024];
groupshared uint visibleCapsuleLightCount;
groupshared uint visibleCapsuleLightIndices[1024];

float3 EvaluatePointLightDiffuse( PointLight light, GBuffer gbuffer )
{
	// Surface-to-light vector
	float3 lightVector = light.PositionVS - gbuffer.PosVS;

	// Compute attenuation based on distance - linear attenuation
	float distToLight = length(lightVector);
	float attenuation = saturate( 1.0f - distToLight / light.Radius);

	// Normalize light vector
	lightVector /= distToLight;

	// Compute diffuse light
	float NdL = max( 0, dot(gbuffer.Normal, lightVector) );
	float3 diffuseLight = NdL * light.Color;

	// Reflection vector
	float3 reflectionVector = normalize(reflect(-lightVector, gbuffer.Normal));

	// Camera-to-surface vector (in VS camera position is zero)
	float3 directionToCamera = normalize(-gbuffer.PosVS);

	// Compute specular light
	float x = saturate( dot( reflectionVector, directionToCamera ) ) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);

	// Take attenuation and light intensity into account
	// TODO: Separate specular from diffuse...
	return float3( attenuation * light.Intensity * diffuseLight + attenuation * light.Intensity * specularLight );
}

float3 EvaluateSpotLightDiffuse( SpotLight light, GBuffer gbuffer )
{
	float3 toLight = light.PositionVS - gbuffer.PosVS;
	float distToLight = length( toLight );
	toLight /= distToLight; // Normalize

	// Linear distance attenuation
	float distAtt = saturate( 1.0f - distToLight * light.RangeRcp );

	// Cone attenuation
	// Angle between lightvector and spot direction (dot) within inner cone: Full
	// attenuation. Outside outer cone: zero attenuation. Between: decrease from
	// 1 to 0.
	float coneAtt = smoothstep( light.CosOuter, light.CosInner, dot( light.DirectionVS, -toLight ) );

	// Diffuse light
	float NdL = saturate( dot( gbuffer.Normal, toLight ) );
	float3 diffuseLight = light.Intensity * NdL * light.Color;

	// Reflection vector
	float3 reflectionVector = normalize( reflect( -toLight, gbuffer.Normal ) );

	// Camera-to-surface vector (in VS camera position is origin)
	float3 directionToCamera = normalize( -gbuffer.PosVS );

	// Specular light
	float x = saturate( dot( reflectionVector, directionToCamera ) ) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);

	// Take attenuation into account
	// TODO: Separate specular from diffuse...
	return distAtt * coneAtt * diffuseLight + distAtt * coneAtt * specularLight;
}

float3 EvaluateCapsuleLightDiffuse( CapsuleLight light, GBuffer gbuffer )
{
	float3 toCapsuleStart = gbuffer.PosVS - light.PositionVS;

	// Project start-to-fragment onto light direction to get distance from
	// light position to closest point on the line (dot product). If this value
	// is negative, we are outside the line from the start point side, which means
	// that the start point is the closest point. If it's greater than the light
	// length (outside line from end point side), the closest point is the end
	// point. Otherwise it's on the line. The value is normalized by dividing
	// by the light length, followed by saturation to clamp in [0,1]. Multiplying
	// this normalized value with the light length, we get correct distance from
	// start point, taking end points into consideration :)
	float distOnLine = dot( toCapsuleStart, light.DirectionVS ) / light.Length;
	distOnLine = saturate(distOnLine) * light.Length;
	float3 pointOnLine = light.PositionVS + light.DirectionVS * distOnLine;
	float3 toLight = pointOnLine - gbuffer.PosVS;
	float distToLight = length(toLight);

	// Diffuse light
	toLight /= distToLight; // Normalize
	float NdL = saturate(dot(toLight, gbuffer.Normal));
	float3 diffuseLight = light.Intensity * NdL * light.Color;

	float3 reflectionVector = normalize(reflect(-toLight, gbuffer.Normal));

	// Camera-to-surface vector (in VS camera position is zero)
	float3 directionToCamera = normalize(-gbuffer.PosVS);

	// Specular light
	float x = saturate(dot(reflectionVector, directionToCamera)) + 1e-6; // Add small epsilon because some graphics processors might return NaN for pow(0,0)
	float specularLight = gbuffer.SpecularIntensity * pow(x, gbuffer.SpecularPower);

	// Linear distance attenuation
	float attenuation = saturate(1.0f - distToLight * light.RangeRcp);

	return attenuation * diffuseLight + attenuation * specularLight;
}

bool IntersectPointLightTile( PointLight light, float4 frustumPlanes[6] )
{
	bool inFrustum = true;
	[unroll]
	for (uint i = 0; i < 6; ++i)
	{
		float dist = dot( frustumPlanes[i], float4( light.PositionVS, 1.0f ) );
		inFrustum = inFrustum && (-light.Radius <= dist);
	}

	return inFrustum;
}

bool IntersectSpotLightTile( SpotLight light, float4 frustumPlanes[6] )
{
	// In lack of better, use sphere/frustum intersection.
	float range = 1 / light.RangeRcp;

	bool inFrustum = true;
	[unroll]
	for (uint i = 0; i < 6; ++i)
	{
		float dist = dot( frustumPlanes[i], float4( light.PositionVS, 1.0f ) );
		inFrustum = inFrustum && (-range <= dist);
	}

	return inFrustum;
}

// This intersection is based on the sphere/plane intersection for point lights. What I do
// here is get startpoint (one end of line) and calculate endpoint (the other end). Now,
// since the capsule defines distance from the line, we basically a sphere at each end.
// Just like with sphere/plane I get the distance from the point to the plane, but I do it
// for both end points for each plane. If any of those spheres are intersecting the frustum,
// the whole capsule does.
bool IntersectCapsuleLightTile( CapsuleLight light, float4 frustumPlanes[6] )
{
	float4 startPoint = float4( light.PositionVS, 1.0f );
	float4 endPoint = float4( light.PositionVS + light.DirectionVS * light.Length, 1.0f );
	float range = 1 / light.RangeRcp;

	bool inFrustum = true;
	[unroll]
	for (uint i = 0; i < 6; ++i)
	{
		float distStart = dot( frustumPlanes[i], startPoint );
		float distEnd = dot( frustumPlanes[i], endPoint );
		inFrustum = inFrustum && ( (-range <= distStart) || (-range <= distEnd) ); // Any of the sphere endpoints inside
	}

	return inFrustum;
}

// One pixel per thread, 16x16 thread groups (= 1 tile).
#define BLOCK_SIZE 16
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CS( uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID,
		 uint groupIndex : SV_GroupIndex, uint3 dispatchThreadID : SV_DispatchThreadID )
{
	// Step 1 - Load gbuffers and depth
	GBuffer gbuffer;

	float depth = gDepthMap.Load( uint3( dispatchThreadID.xy, 0 ) ).r;
	float4 diffuse_specIntensity = gColorMap.Load( uint3( dispatchThreadID.xy, 0 ) );
	float4 normal_specPower = gNormalMap.Load( uint3( dispatchThreadID.xy, 0 ) );

	// Reconstruct view space position from depth
	float x = (dispatchThreadID.x / gBackbufferWidth) * 2 - 1;
	float y = (1 - dispatchThreadID.y / gBackbufferHeight) * 2 - 1;
	float4 posVS = mul( float4( x, y, depth, 1 ), gInvProj );
	posVS /= posVS.w;
	
	gbuffer.PosVS = posVS.xyz;
	gbuffer.Diffuse = diffuse_specIntensity.rgb;
	gbuffer.Normal = 2.0f * normal_specPower.xyz - 1.0f;
	gbuffer.SpecularIntensity = diffuse_specIntensity.a;
	gbuffer.SpecularPower = normal_specPower.a * 255;

	// Initialize group shared memory
	if (groupIndex == 0)
	{
		visiblePointLightCount = 0;
		visibleSpotLightCount = 0;
		visibleCapsuleLightCount = 0;
		minDepth = 0xFFFFFFFF;
		maxDepth = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	// Step 2 - Calculate min and max z in threadgroup / tile
	float linearDepth = gProj[3][2] / (depth - gProj[2][2]);
	uint depthInt = asuint( linearDepth );

	// Only works on ints, but we can cast to int because z is always positive
	// Set group minimum
	InterlockedMin( minDepth, depthInt );
	InterlockedMax( maxDepth, depthInt );

	GroupMemoryBarrierWithGroupSync();

	// When group min/max have been calculated, we use those values
	float minGroupDepth = asfloat( minDepth );
	float maxGroupDepth = asfloat( maxDepth );

	// Step 3 - Culling
	// Determine visible light sources for each tile
	// * Cull all light sources against tile frustum (jag antar att de olika trådarna
	// kan culla vars ett ljus i listan tills alla ljus är cullade)
	// Input (global): Light list (frustum and SW occlusion culled, (pure list for me)
	// Output (per tile):
	// * # of visible light sources
	// * Index list of visible light sources
	// ------------------|-Lights---|--Indices----------------------
	// Global list		 |	1000+	 |	0 1 2 3 4 5 6 7 8 ..		|
	// Tile visible list |	~0-40+	 |  0 2 5 6 8 ..				|
	// --------------------------------------------------------------

	// 3a: Each thread switches to process lights instead of pixels
	// - 256 lights sources in parallell
	// - Multiple iterations for > 256 lights
	// 3b: Intersect light and tile
	// - Tile min and max z is used as a "depth bounds" test
	// 3c: Append visible light indices to list
	// - Atomic add to threadgroup shared memory
	// 3d: Switch back to processing pixels
	// - Synchronize thread group
	// - We now know which light sources affect the tile

	// Calculate tile frustum
	// Extrahering av plan kan ses i RTR (s.774), där skillnaden är att kolumner
	// används istället för rader eftersom DX är vänsterorienterat. Här bryr vi
	// oss inte om att negera planen eftersom vi vill att de ska peka in i frustum.
	// Enbart projektionsmatrisen används eftersom vi då kommer arbeta i view space.
	// Vidare är de använda kolumnerna modifierade för rutans frustum.
	float4 frustumPlanes[6];
	// TODO: Bara BLOCK_SIZE istället för 2 * ?
	float2 tileScale = float2(gBackbufferWidth, gBackbufferHeight) * rcp( float( 2 * BLOCK_SIZE ) );
	float2 tileBias = tileScale - float2( groupID.xy );
	float4 col1 = float4( gProj._11 * tileScale.x, 0.0f, tileBias.x, 0.0f );
	float4 col2 = float4( 0.0f, -gProj._22 * tileScale.y, tileBias.y, 0.0f );
	float4 col4 = float4( 0.0f, 0.0f, 1.0f, 0.0f );
	frustumPlanes[0] = col4 + col1; // Left plane
	frustumPlanes[1] = col4 - col1; // Right plane
	frustumPlanes[2] = col4 - col2; // Top plane
	frustumPlanes[3] = col4 + col2; // Bottom plane
	// Remember: First three components are the plane normal (a,b,c). The last
	// component w = -(ax + by + cz) where (x,y,z) is a point on the plane.
	frustumPlanes[4] = float4( 0.0f, 0.0f, 1.0f, -minGroupDepth ); // Near plane
	frustumPlanes[5] = float4( 0.0f, 0.0f, -1.0f, maxGroupDepth ); // Far plane
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		// Normalize planes (near and far already normalized)
		frustumPlanes[i] *= rcp( length( frustumPlanes[i].xyz ) );
	}

	// TODO:
	// Det jag gör nu är lite av en naiv lösning. Jag låter trådar culla ljus precis som
	// vanligt. Om det är fler ljus än trådar körs flera iterationer till alla ljus är cullade.
	// Därefter gör jag samma sak fast för spot light. "Problemet", om man kan kalla det så,
	// ligger i att de första trådarna arbetar med första ljustypen, till alla är cullade, 
	// varpå trådarna börjar med nästa typ. Under tiden som en ljustyp cullas sitter oanvända
	// trådar bara och väntar. Det hade varit bra om de som inte används (när det är fler trådar
	// än ljus som ska cullas) kunde börja culla nästa ljustyp istället. Jag vet dock inte hur
	// detta ska göras om man ska undvika massa if-satser och bara kunna köra allt seriellt.
	// Det är ju trots allt andra funktioner som ska köras för intersektionstest. Liknande
	// gäller nedan där ljusen summeras.
	// Cull lights against computed frustum
	uint threadCount = BLOCK_SIZE * BLOCK_SIZE;
	uint passCount = (gPointLightCount + threadCount - 1) / threadCount;
	uint passIt;
	for (passIt = 0; passIt < passCount; ++passIt)
	{
		uint lightIndex = passIt * threadCount + groupIndex;

		// Prevent overrun by clamping to a last "null" light
		// TODO: När vi clampar till null-ljus vill vi se till att ljuset har
		// egenskaper som alltid misslyckas intersection med frustum (och gärna
		// snabbt). Det betyder att ljuset aldrig ens kommer beräknas på senare,
		// även om det inte skulle påverka resultat. Men vi sparar in lite beräkningar.
		lightIndex = min( lightIndex, gPointLightCount );
		
		PointLight light = gPointLights[lightIndex];
		
		if (IntersectPointLightTile( light, frustumPlanes ))
		{
			uint offset;
			InterlockedAdd( visiblePointLightCount, 1, offset );
			visiblePointLightIndices[offset] = lightIndex;
		}
	}

	passCount = (gSpotLightCount + threadCount - 1) / threadCount;
	for (passIt = 0; passIt < passCount; ++passIt)
	{
		uint lightIndex = passIt * threadCount + groupIndex;
		
		// Prevent overrun by clamping to a last "null" light
		lightIndex = min( lightIndex, gSpotLightCount );

		SpotLight light = gSpotLights[lightIndex];

		if (IntersectSpotLightTile( light, frustumPlanes ))
		{
			uint offset;
			InterlockedAdd( visibleSpotLightCount, 1, offset );
			visibleSpotLightIndices[offset] = lightIndex;
		}
	}

	passCount = (gCapsuleLightCount + threadCount - 1) / threadCount;
	for (passIt = 0; passIt < passCount; ++passIt)
	{
		uint lightIndex = passIt * threadCount + groupIndex;
		
		// Prevent overrun by clamping to a last "null" light
		lightIndex = min( lightIndex, gCapsuleLightCount );

		CapsuleLight light = gCapsuleLights[lightIndex];

		if (IntersectCapsuleLightTile( light, frustumPlanes ))
		{
			uint offset;
			InterlockedAdd( visibleCapsuleLightCount, 1, offset );
			visibleCapsuleLightIndices[offset] = lightIndex;
		}
	}

	GroupMemoryBarrierWithGroupSync();

	// 4: For each pixel, accumulate lighting from visible lights
	// - Read from tile visible light index list in groupshared memory

	// 5: Combine lighting and shading albedos
	// - Output is non-MSAA HDR texture
	
	float3 color = 0.3f * gbuffer.Diffuse; // Ambient
	
	uint lightIt;
	for (lightIt = 0; lightIt < visiblePointLightCount; ++lightIt)
	{
		uint lightIndex = visiblePointLightIndices[lightIt];
		PointLight light = gPointLights[lightIndex];

		color += gbuffer.Diffuse * EvaluatePointLightDiffuse( light, gbuffer );
		//color += diffuseAlbedo * evaluateLightDiffuse( light, gbuffer );
		//color += specularAlbedo * evaluateLightSpecular( light, gbuffer );
	}

	for (lightIt = 0; lightIt < visibleSpotLightCount; ++lightIt)
	{
		uint lightIndex = visibleSpotLightIndices[lightIt];
		SpotLight light = gSpotLights[lightIndex];

		color += gbuffer.Diffuse * EvaluateSpotLightDiffuse( light, gbuffer );
	}

	for (lightIt = 0; lightIt < visibleCapsuleLightCount; ++lightIt)
	{
		uint lightIndex = visibleCapsuleLightIndices[lightIt];
		CapsuleLight light = gCapsuleLights[lightIndex];

		color += gbuffer.Diffuse * EvaluateCapsuleLightDiffuse( light, gbuffer );
	}

	gOutputTexture[dispatchThreadID.xy] = float4( color, 1 );
}

technique11 Tech
{
	pass p0
	{
		SetVertexShader( NULL );
		SetGeometryShader( NULL );
		SetPixelShader( NULL );
		SetComputeShader( CompileShader( cs_5_0, CS() ) );
	}
}