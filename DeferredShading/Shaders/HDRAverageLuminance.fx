Texture2D HDRTex : register( t0 );
StructuredBuffer<float> AverageValues1D : register( t0 ); // Används i andra passet, därför de är på samma register
RWStructuredBuffer<float> AverageLum : register( u0 );

cbuffer DownScaleConstants : register( b0 )
{
	uint2 Res : packoffset( c0 ); // Resolution of the downscaled target
	uint Domain : packoffset( c0.z ); // Total pixels in the downscaled image
	uint GroupSize : packoffset( c0.w ); // Number of groups dispatched on the first pass
}

groupshared float SharedPositions[1024]; // Shared memory to store intermediate results

static const float4 LUM_FACTOR = float4(0.299, 0.587, 0.114, 0);

// Initial 4x4 downscale on each thread
float DownScale4x4( uint2 CurPixel, uint groupThreadID )
{
	float avgLum = 0.0;
	
	// Skip out of bounds pixels
	if (CurPixel.y < Res.y)
	{
		// Sum a group a group of 4x4 pixels
		int3 nFullResPos = int3(CurPixel * 4, 0);

		float4 downScaled = float4(0.0, 0.0, 0.0, 0.0);
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			[unroll]
			for (int j = 0; j < 4; ++j)
			{
				// Andra argumentet är offset på texturkoordinaten (det är INTE sampleindex eftersom Texture2D ej är multisample)
				downScaled += HDRTex.Load( nFullResPos, int2(j, i) );
			}
		}
		downScaled /= 16.0;

		// Calculate the luminance value for this pixel
		avgLum = dot( downScaled, LUM_FACTOR );
		
		// Write the result to the shared memory
		SharedPositions[groupThreadID] = avgLum;
	}

	// Synchronize before next step
	GroupMemoryBarrierWithGroupSync();

	return avgLum;
}

// Continues downscale down to four values
float DownScale1024to4( uint dispatchThreadID, uint groupThreadID, float avgLum )
{
	[unroll]
	for (uint groupSize = 4, step1 = 1, step2 = 2, step3 = 3; groupSize < 1024; groupSize *= 4, step1 *= 4, step2 *= 4, step3 *= 4)
	{
		// Skip out of bounds pixels
		if (groupThreadID % groupSize == 0)
		{
			// Calculate the luminance sum for this step
			float stepAvgLum = avgLum;
			stepAvgLum += dispatchThreadID + step1 < Domain ? SharedPositions[groupThreadID + step1] : avgLum;
			stepAvgLum += dispatchThreadID + step2 < Domain ? SharedPositions[groupThreadID + step2] : avgLum;
			stepAvgLum += dispatchThreadID + step3 < Domain ? SharedPositions[groupThreadID + step3] : avgLum;

			// Store the results
			avgLum = stepAvgLum;
			SharedPositions[groupThreadID] = stepAvgLum;
		}

		// Synchronize before next step
		GroupMemoryBarrierWithGroupSync();
	}

	return avgLum;
}

// Downscale 4 values to one averaged.
void DownScale4to1( uint dispatchThreadID, uint groupThreadID, uint groupID, float avgLum )
{
	if (groupThreadID == 0)
	{
		// Calculate the average luminance for this thread group
		float fFinalAvgLum = avgLum;
		fFinalAvgLum += dispatchThreadID + 256 < Domain ? SharedPositions[groupThreadID + 256] : avgLum;
		fFinalAvgLum += dispatchThreadID + 512 < Domain ? SharedPositions[groupThreadID + 512] : avgLum;
		fFinalAvgLum += dispatchThreadID + 768 < Domain ? SharedPositions[groupThreadID + 768] : avgLum;
		fFinalAvgLum /= 1024.0;

		// Write the final value into the 1D UAV which will be used in the next step.
		AverageLum[groupID] = fFinalAvgLum;
	}
}

[numthreads(1024, 1, 1)]
void DownScaleFirstPass( uint3 groupID : SV_GroupID, uint3 dispatchThreadID : SV_DispatchThreadID,
						uint3 groupThreadID : SV_GroupThreadID )
{
	uint2 CurPixel = uint2( dispatchThreadID.x % Res.x, dispatchThreadID.x / Res.x );

	// Reduce a group of 16 pixels to a single pixel and store in the shared memory
	float avgLum = DownScale4x4(CurPixel, groupThreadID.x);

	// Downscale from 1024 to 4
	avgLum = DownScale1024to4(dispatchThreadID.x, groupThreadID.x, avgLum);

	// Downscale from 4 to 1
	DownScale4to1(dispatchThreadID.x, groupThreadID.x, groupID.x, avgLum);
}


// --------------------------------------------------------------------------
// Second pass converts 1D average values into a single value
// --------------------------------------------------------------------------

#define MAX_GROUPS 64

// Group shared memory to store the intermediate results
groupshared float SharedAvgFinal[MAX_GROUPS];

[numthreads(MAX_GROUPS, 1, 1)]
void DownScaleSecondPass(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID)
{
	// Fill the shared memory with the 1D values from the first pass
	float avgLum = 0.0;
	if (dispatchThreadID.x < GroupSize)
	{
		avgLum = AverageValues1D[dispatchThreadID.x];
	}

	SharedAvgFinal[dispatchThreadID.x] = avgLum;

	GroupMemoryBarrierWithGroupSync();

	// Downscale from 64 to 16
	if (dispatchThreadID.x % 4 == 0)
	{
		// Calculate the luminance sum for this step
		float stepAvgLum = avgLum;
		stepAvgLum += dispatchThreadID.x + 1 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 1] : avgLum;
		stepAvgLum += dispatchThreadID.x + 2 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 2] : avgLum;
		stepAvgLum += dispatchThreadID.x + 3 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 3] : avgLum;

		// Store the results
		avgLum = stepAvgLum;
		SharedAvgFinal[dispatchThreadID.x] = stepAvgLum;
	}

	GroupMemoryBarrierWithGroupSync();

	// Downscale from 16 to 4
	if (dispatchThreadID.x % 16 == 0)
	{
		// Calculate the luminance sum for this step
		float stepAvgLum = avgLum;
		stepAvgLum += dispatchThreadID.x + 4 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 4] : avgLum;
		stepAvgLum += dispatchThreadID.x + 8 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 8] : avgLum;
		stepAvgLum += dispatchThreadID.x + 12 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 12] : avgLum;

		// Store the results
		avgLum = stepAvgLum;
		SharedAvgFinal[dispatchThreadID.x] = stepAvgLum;
	}

	GroupMemoryBarrierWithGroupSync();

	// Downscale from 4 to 1
	if (dispatchThreadID.x == 0)
	{
		// Calculate the average luminance
		float fFinalLumValue = avgLum;
		fFinalLumValue += dispatchThreadID.x + 16 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 16] : avgLum;
		fFinalLumValue += dispatchThreadID.x + 32 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 32] : avgLum;
		fFinalLumValue += dispatchThreadID.x + 48 < GroupSize ? SharedAvgFinal[dispatchThreadID.x + 48] : avgLum;
		fFinalLumValue /= 64.0;

		AverageLum[0] = max(fFinalLumValue, 0.0001);
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