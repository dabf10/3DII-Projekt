// 1 tråd per pixel, 16x16 trådgrupper (tile)

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

struct Light
{
	float3 PositionVS;
	float Radius;
	float3 Color;
	float Intensity;
};
int gLightCount;
//StructuredBuffer<Light> gLights;
Light gLights[10]; // TODO: Ändra till ovan. Då måste jag fixa med buffrar och sånt i c++ kod. Är det kanske nu man använder map och sånt för att uppdatera buffern?

float4x4 gProj;
float4x4 gInvProj;
float gBackbufferWidth;
float gBackbufferHeight;

groupshared uint minDepth;
groupshared uint maxDepth;

groupshared uint visibleLightCount;
groupshared uint visibleLightIndices[1024];

float3 EvaluateLightDiffuse( Light light, GBuffer gbuffer )
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

#define BLOCK_SIZE 16
[numthreads(BLOCK_SIZE, BLOCK_SIZE, 1)]
void CS( uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID,
		 uint groupIndex : SV_GroupIndex, uint3 dispatchThreadID : SV_DispatchThreadID )
{
	// Step 1 - Load gbuffers and depth
	GBuffer gbuffer;

	// TODO: Depth är inte linjärt direkt när man laddar, men det kanske inte behövs
	float depth = gDepthMap.Load( uint3( dispatchThreadID.xy, 0 ) ).r;
	float4 diffuse_specIntensity = gColorMap.Load( uint3( dispatchThreadID.xy, 0 ) );
	float4 normal_specPower = gNormalMap.Load( uint3( dispatchThreadID.xy, 0 ) );

	// Reconstruct position from depth
	float x = (dispatchThreadID.x / gBackbufferWidth) * 2 - 1;
	float y = (1 - dispatchThreadID.y / gBackbufferHeight) * 2 - 1;
	float4 posH = float4( x, y, depth, 1 );
	float4 posVS = mul( posH, gInvProj );
	posVS /= posVS.w;
	
	gbuffer.PosVS = posVS.xyz;
	gbuffer.Diffuse = diffuse_specIntensity.rgb;
	gbuffer.Normal = 2.0f * normal_specPower.xyz - 1.0f;
	gbuffer.SpecularIntensity = diffuse_specIntensity.a;
	gbuffer.SpecularPower = normal_specPower.a * 255;

	// Initialize group shared memory
	if (groupIndex == 0)
	{
		visibleLightCount = 0;
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

	uint threadCount = BLOCK_SIZE * BLOCK_SIZE;
	uint passCount = (gLightCount + threadCount - 1) / threadCount;

	for (uint passIt = 0; passIt < passCount; ++passIt)
	{
		uint lightIndex = passIt * threadCount + groupIndex;

		// Prevent overrun by clamping to a last "null" light
		// TODO: När vi clampar till null-ljus vill vi se till att ljuset har
		// egenskaper som alltid misslyckas intersection med frustum (och gärna
		// snabbt). Det betyder att ljuset aldrig ens kommer beräknas på senare,
		// även om det inte skulle påverka resultat. Men vi sparar in lite beräkningar.
		lightIndex = min( lightIndex, gLightCount );
		
		// Intersection code begin
		Light light = gLights[lightIndex];
			// Position already in VS :)
		bool inFrustum = true;
		[unroll]
		for (uint i = 0; i < 6; ++i)
		{
			float dist = dot( frustumPlanes[i], float4( light.PositionVS, 1.0f ) );
			inFrustum = inFrustum && (-light.Radius <= dist);
		}

		if (inFrustum)
		{
			uint offset;
			InterlockedAdd( visibleLightCount, 1, offset );
			visibleLightIndices[offset] = lightIndex;
		}
		// Intersection code end

		//if (intersects(gLights[lightIndex], tile))
		//{
		//	uint offset;
		//	InterlockedAdd(visibleLightCount, 1, offset);
		//	visibleLightIndices[offset] = lightIndex;
		//}
		//visibleLightCount = gLightCount; // TODO: remove
		//visibleLightIndices[lightIndex] = lightIndex; // TODO: remove
	}

	GroupMemoryBarrierWithGroupSync();

	// 4: For each pixel, accumulate lighting from visible lights
	// - Read from tile visible light index list in groupshared memory

	// 5: Combine lighting and shading albedos
	// - Output is non-MSAA HDR texture
	
	float3 color = 0.3f * gbuffer.Diffuse; // Ambient
	
	for (uint lightIt = 0; lightIt < visibleLightCount; ++lightIt)
	{
		uint lightIndex = visibleLightIndices[lightIt];
		Light light = gLights[lightIndex];

		color += gbuffer.Diffuse * EvaluateLightDiffuse( light, gbuffer );
		//color += diffuseAlbedo * evaluateLightDiffuse( light, gbuffer );
		//color += specularAlbedo * evaluateLightSpecular( light, gbuffer );
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