/* Beräkningen består av två pass. Första passet reducerar bilden till en 1D textur,
som innehåller medelvärdet för varje trådgrupp. 1D texturen kommer således innehålla
lika många element som antal trådgrupper som körs. Andra passet reducerar denna
1D textur till ett slutligt värde genom att beräkna medelvärdet för dess texlar.

Varje tråd i första passet beräknar medelvärdet av 4x4 pixlar, och varje trådgrupp
kör 1024 trådar (max möjliga). Antal trådgrupper som måste köras i x är alltså
					ceil(width * height / (16.f * 1024.f)).
Antal trådgrupper på y och z är 1.
*/

Texture2D gHDRTex : register( t0 );
RWStructuredBuffer<float> gAverageLumOutput : register( u0 );

cbuffer DownScaleConstants : register( b0 )
{
	uint2 gDownscaleRes : packoffset( c0 ); // width and height divided by 4
	uint gDownscaleNumPixels : packoffset( c0.z ); // Total pixels in the downscaled image
	uint gGroupCount : packoffset( c0.w ); // Number of groups dispatched on the first pass
}

// Shared memory to store intermediate results
groupshared float SharedAverage[1024];

static const float4 LUM_FACTOR = float4(0.299, 0.587, 0.114, 0);

// Initial 4x4 downscale on each thread
void DownScale4x4( uint2 curPixel, uint groupThreadID, out float avgLum )
{
	avgLum = 0.0;
	
	// Skip out of bounds pixels
	if (curPixel.y < gDownscaleRes.y)
	{
		// Sum a group a group of 4x4 pixels
		int3 fullResPos = int3(curPixel * 4, 0);

		float luminance = 0.0;
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			[unroll]
			for (int j = 0; j < 4; ++j)
			{
				// TODO: Kan dessa fyra texture fetches i inre loopen ersättas med gather4?
				// Andra argumentet är offset på texturkoordinaten (det är INTE sampleindex eftersom Texture2D ej är multisample)
				luminance = dot( gHDRTex.Load( fullResPos, int2(j, i) ), LUM_FACTOR );
				avgLum += log( 1e-3 + luminance );
				//avgLum += luminance;
			}
		}
		avgLum /= 16.0;
		
		// Write the result to the shared memory
		SharedAverage[groupThreadID] = avgLum;
	}

	// Synchronize before next step
	GroupMemoryBarrierWithGroupSync();
}

// Continues downscale down to four values
void DownScale1024to4( uint dispatchThreadID, uint groupThreadID, inout float avgLum )
{
	// mod is used to work with every 4th of the previous threads.
	// steps are used to locate where to get sums calculated by previous threads.
	// step1 -> 1 -> 4 -> 16 and so forth
	// step2 -> 2 -> 8 -> 32 and so forth
	// step3 -> 3 -> 12 -> 48 and so forth
	[unroll]
	for (uint mod = 4, step1 = 1, step2 = 2, step3 = 3; mod < 1024; mod *= 4, step1 *= 4, step2 *= 4, step3 *= 4)
	{
		// Skip out of bounds pixels
		if (groupThreadID % mod == 0)
		{
			// Calculate the luminance sum for this step
			float stepAvgLum = avgLum;
			stepAvgLum += dispatchThreadID + step1 < gDownscaleNumPixels ? SharedAverage[groupThreadID + step1] : avgLum;
			stepAvgLum += dispatchThreadID + step2 < gDownscaleNumPixels ? SharedAverage[groupThreadID + step2] : avgLum;
			stepAvgLum += dispatchThreadID + step3 < gDownscaleNumPixels ? SharedAverage[groupThreadID + step3] : avgLum;

			// Store the results
			avgLum = stepAvgLum;
			SharedAverage[groupThreadID] = stepAvgLum;
		}

		// Synchronize before next step
		GroupMemoryBarrierWithGroupSync();
	}
}

// Downscale 4 values to one averaged.
void DownScale4to1( uint dispatchThreadID, uint groupThreadID, uint groupID, float avgLum )
{
	// The first thread of every group sums up the final value for its group and
	// divides by number of threads in the group to get average value. This value
	// is output for this group.
	if (groupThreadID == 0)
	{
		// Calculate the average luminance for this thread group
		float finalAvgLum = avgLum;
		finalAvgLum += dispatchThreadID + 256 < gDownscaleNumPixels ? SharedAverage[groupThreadID + 256] : avgLum;
		finalAvgLum += dispatchThreadID + 512 < gDownscaleNumPixels ? SharedAverage[groupThreadID + 512] : avgLum;
		finalAvgLum += dispatchThreadID + 768 < gDownscaleNumPixels ? SharedAverage[groupThreadID + 768] : avgLum;
		finalAvgLum /= 1024.0;

		// Write the final value into the 1D UAV which will be used in the next step.
		gAverageLumOutput[groupID] = finalAvgLum;
	}
}

[numthreads(1024, 1, 1)]
void DownScaleFirstPass( uint3 groupID : SV_GroupID, uint3 dispatchThreadID : SV_DispatchThreadID,
						uint3 groupThreadID : SV_GroupThreadID )
{
	float avgLum = 0.0;

	uint2 curPixel = uint2( dispatchThreadID.x % gDownscaleRes.x, dispatchThreadID.x / gDownscaleRes.x );

	// Every thread reduces a group of 16 pixels to a single pixel and stores in
	// the shared memory.
	DownScale4x4( curPixel, groupThreadID.x, avgLum );

	// Downscale the 1024 values to 4
	DownScale1024to4( dispatchThreadID.x, groupThreadID.x, avgLum );

	// The first thread of the group downscales the last 4 average values to a
	// final average for this thread group.
	DownScale4to1( dispatchThreadID.x, groupThreadID.x, groupID.x, avgLum );
}


// --------------------------------------------------------------------------
// Second pass converts 1D average values into a single value
// --------------------------------------------------------------------------

// Enough to handle 1080p res (16 pixels per thread, 1024 threads per group)
// Note: MAX_GROUPS power of 4. Reductions is how many iterations - 1 we need to
// reduce max_groups to 1. For example: If max_groups is 256 we need 4 iterations
// to reduce it to 1. We set 4 - 1 = 3 because the last 4 to 1 reduction is
// slightly different.
#define MAX_GROUPS 256
#define REDUCTIONS 3

StructuredBuffer<float> gAverageValues1D : register( t0 );

// Group shared memory to store the intermediate results
groupshared float SharedAvgFinal[MAX_GROUPS];

[numthreads(MAX_GROUPS, 1, 1)]
void DownScaleSecondPass( uint3 dispatchThreadID : SV_DispatchThreadID )
{
	// Fill the shared memory with the 1D values from the first pass. Threads
	// that corresponds to a group that was never executed simply adds 0.
	float avgLum = 0.0;
	if (dispatchThreadID.x < gGroupCount)
	{
		avgLum = gAverageValues1D[dispatchThreadID.x];
	}

	SharedAvgFinal[dispatchThreadID.x] = avgLum;

	GroupMemoryBarrierWithGroupSync();

	// Really, we could let the first thread sum everything up and divide by number
	// of groups because that's the point of this pass, but since we have a couple
	// of threads they might as well help their friend out. Every iteration downscales
	// to a quarter until we have 4 values (we stop here to handle the last step
	// separately).

	// The steps are used to locate where to get sums calculated earlier.
	uint step1 = 1; // 1 -> 4 -> 16 and so forth
	uint step2 = 2; // 2 -> 8 -> 32 and so forth
	uint step3 = 3; // 3 -> 12 -> 48 and so forth
	[unroll]
	for (uint i = 0; i < REDUCTIONS; ++i, step1 *= 4, step2 *= 4, step3 *= 4)
	{
		// Work with every 4th of the previous threads.
		if (dispatchThreadID.x % (step1 * 4) == 0)
		{
			// Sum up luminance
			avgLum += dispatchThreadID.x + step1 < gGroupCount ? SharedAvgFinal[dispatchThreadID.x + step1] : 0.0;
			avgLum += dispatchThreadID.x + step2 < gGroupCount ? SharedAvgFinal[dispatchThreadID.x + step2] : 0.0;
			avgLum += dispatchThreadID.x + step3 < gGroupCount ? SharedAvgFinal[dispatchThreadID.x + step3] : 0.0;

			// Store results
			SharedAvgFinal[dispatchThreadID.x] = avgLum;
		}

		// Synchronize before next step
		GroupMemoryBarrierWithGroupSync();
	}

	// When the 1D texture has been downscaled to 4 texels, the very first thread
	// sums up those texels just like before, but instead of storing the value
	// we divide by the number of groups to get average and return it.
	if (dispatchThreadID.x == 0)
	{
		// Sum up luminance
		avgLum += dispatchThreadID.x + step1 < gGroupCount ? SharedAvgFinal[dispatchThreadID.x + step1] : 0.0;
		avgLum += dispatchThreadID.x + step2 < gGroupCount ? SharedAvgFinal[dispatchThreadID.x + step2] : 0.0;
		avgLum += dispatchThreadID.x + step3 < gGroupCount ? SharedAvgFinal[dispatchThreadID.x + step3] : 0.0;

		// Return average/maximum luminance.
		gAverageLumOutput[0] = exp( max( avgLum / gGroupCount, 0.0001 ) );
		//gAverageLumOutput[0] = max( avgLum / gGroupCount, 0.0001 );
	}
}

technique11 AverageLuminance
{
	pass p0
	{
		SetComputeShader( CompileShader( cs_5_0, DownScaleFirstPass() ) );
	}

	pass p1
	{
		SetComputeShader( CompileShader( cs_5_0, DownScaleSecondPass() ) );
	}
}