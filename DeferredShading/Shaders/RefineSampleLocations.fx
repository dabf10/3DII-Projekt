float4 gLightScreenPos;
uint gMaxSamplesInSlice;

#ifndef INITIAL_SAMPLE_STEP
#	define INITIAL_SAMPLE_STEP 128
#endif
#ifndef THREAD_GROUP_SIZE
#	define THREAD_GROUP_SIZE max(INITIAL_SAMPLE_STEP, 32)
#endif
Texture2D<float2> gCoordinates : register( t1 );
Texture2D<float> gEpipolarCamSpaceZ : register( t2 );
RWTexture2D<uint2> gInterpolationSource : register( u0 );
// Packing 32 flags into a single uint value.
static const uint gNumPackedFlags = THREAD_GROUP_SIZE/32;
groupshared uint gPackedCamSpaceDiffFlags[ gNumPackedFlags ];
float gRefinementThreshold;
uint gEpipoleSamplingDensityFactor;

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void RefineSampleLocationsCS( uint3 groupID : SV_GroupID,
							  uint3 groupThreadID : SV_GroupThreadID )
{
	// Each thread group processes one slice
	uint sliceIndex = groupID.y;

	// Compute global index of the first sample in the thread group.
	// Each group processes THREAD_GROUP_SIZE samples in the slice.
	uint groupStartGlobalIndex = groupID.x * THREAD_GROUP_SIZE;

	// Sample index in the group
	uint sampleIndex = groupThreadID.x;
	
	// Compute global index of this sample which is required to fetch the sample's
	// coordinates. // HEST: ThreadDispatchID???
	uint globalSampleIndex = groupStartGlobalIndex + sampleIndex;

	// Load location of the current sample using global sample index.
	float2 sampleLocationPS = gCoordinates.Load( uint3(globalSampleIndex, sliceIndex, 0) );

	bool isValidThread = all( abs(sampleLocationPS) < 1+1e-4 );

	// Initialize flags with zeroes
	if (groupThreadID.x < gNumPackedFlags)
		gPackedCamSpaceDiffFlags[groupThreadID.x] = 0;

	// Wait for the other threads to reach here (texture fetches may not complete
	// at the same time).
	GroupMemoryBarrierWithGroupSync();

	// Let each thread in the thread group compute its own flag.
	// Note that if the sample is located behind the screen, its flag will be set
	// to zero. Besides, since gEpipolarCamSpaceZ is cleared with invalid
	// coordinates, the difference flag between valid and invalid locations will
	// also be zero. Thus the sample next to invalid will always be marked as ray
	// marching sample.
	[branch]
	if (isValidThread)
	{
		// Load camera space Z for this sample and for its right neighbor (remember
		// to use global sample index).
		bool flag;
		float camSpaceZ = gEpipolarCamSpaceZ.Load( uint3( globalSampleIndex, sliceIndex, 0 ) );
		float rightNeighborCamSpaceZ = gEpipolarCamSpaceZ.Load( uint3( globalSampleIndex + 1, sliceIndex, 0 ) );

		// Compare the difference with the threshold.
		flag = abs(camSpaceZ - rightNeighborCamSpaceZ) < gRefinementThreshold;

		// Set appropriate flag using INTERLOCKED Or:
		InterlockedOr( gPackedCamSpaceDiffFlags[sampleIndex / 32], flag << (sampleIndex % 32) );
	}

	// Synchronize threads in the group
	GroupMemoryBarrierWithGroupSync();

	// Skip invalid threads. This can be done only after the synchronization.
	if (!isValidThread)
		return;

	//							   initialSampleStep
	//		sampleIndex				 |<--------->|
	//			|					 |			 |
	//		X   *  *  *  X  *  *  *  X  *  *  *  X		X - locations of initial samples
	//		|			 |
	//		|			initialSample1Index
	//	initialSample0Index
	//
	// Find two closest initial ray marching samples.
	uint initialSampleStep = INITIAL_SAMPLE_STEP;
	uint initialSample0Index = (sampleIndex / initialSampleStep) * initialSampleStep;
	
	// Use denser sampling near the epipole to account for high variation.
	// Note that sampling near the epipole is very cheap since only a few steps
	// are required to perform ray marching.
	uint initialSample0GlobalIndex = initialSample0Index + groupStartGlobalIndex;
	float2 initialSample0Coords = gCoordinates.Load( uint3( initialSample0GlobalIndex, sliceIndex, 0 ) );
	if (initialSample0GlobalIndex / (float)gMaxSamplesInSlice < 0.1 &&
		length(initialSample0Coords - gLightScreenPos.xy) < 0.3 )
	{
		initialSampleStep = max( INITIAL_SAMPLE_STEP / gEpipoleSamplingDensityFactor, 1 );
		initialSample0Index = (sampleIndex / initialSampleStep) * initialSampleStep;
	}
	uint initialSample1Index = initialSample0Index + initialSampleStep;

	// Remember that the last sample in each epipolar slice must be ray marching one.
	uint interpolationTexWidth, interpolationTexHeight;
	gInterpolationSource.GetDimensions(interpolationTexWidth, interpolationTexHeight);
	if (groupID.x == interpolationTexWidth / THREAD_GROUP_SIZE - 1 )
		initialSample1Index = min(initialSample1Index, THREAD_GROUP_SIZE - 1);

	uint leftSrcSampleIndex = sampleIndex;
	uint rightSrcSampleIndex = sampleIndex;

	// Do nothing if sample is one of initial samples. In this case the sample will
	// be interpolated from itself.
	if (sampleIndex > initialSample0Index && sampleIndex < initialSample1Index)
	{
		// Load group shared memory to the thread local memory.
		uint packedCamSpaceDiffFlags[gNumPackedFlags];
		for (uint i = 0; i < gNumPackedFlags; ++i)
			packedCamSpaceDiffFlags[i] = gPackedCamSpaceDiffFlags[i];

		// Check if there are no depth breaks in the whole section.
		// In such case all the flags are set.
		bool noDepthBreaks = true;
#if INITIAL_SAMPLE_STEP < 32
		{
			// Check if all initialSampleStep flags starting from position
			// initialSample0Index are set:
			int flagPackOrder = initialSample0Index / 32;
			int flagOrderInPack = initialSample0Index % 32;
			uint flagPack = packedCamSpaceDiffFlags[flagPackOrder];
			uint allFlagsMask = ((1 << initialSampleStep) - 1);
			if ( ((flagPack >> flagOrderInPack) & allFlagsMask) != allFlagsMask )
				noDepthBreaks = false;
		}
#else
		{
			for (uint i = 0; i < gNumPackedFlags; ++i)
				if (packedCamSpaceDiffFlags[i] != 0xFFFFFFFFU)
					// If at least one flag is not set, there is a depth break on
					// this section.
					noDepthBreaks = false;
		}
#endif

		if (noDepthBreaks)
		{
			// If there are no depth breaks, we can skip all calculations and use
			// initial sample locations as interpolation sources:
			leftSrcSampleIndex = initialSample0Index;
			rightSrcSampleIndex = initialSample1Index;
		}
		else
		{
			// Find left interpolation source
			{
				// Note that i-th flag reflects the difference between i-th and
				// (i+1)-th samples:
				// Flag[i] = abs(camSpaceZ[i] - camSpaceZ[i+1]) < gRefinementThreshold;
				// We need to find first depth break starting from
				// firstDepthBreakToTheLeftIndex sample and going to the left up
				// to initialSample0Index.
				int firstDepthBreakToTheLeftIndex = sampleIndex-1;
				//															firstDepthBreakToTheLeftIndex
				//																	 |
				//																	 v
				//		0  1  2  3						30 31   32 33	....	i-1  i  i+1 .... 63  64
				//	|										  |							 1  1  1  1 |
				//			packedCamSpaceDiffFlags[0]				packedCamSpaceDiffFlags[1]
				//
				//		flagOrderInPack == i % 32

				int flagPackOrder = uint(firstDepthBreakToTheLeftIndex) / 32;
				int flagOrderInPack = uint(firstDepthBreakToTheLeftIndex) % 32;
				uint flagPack = packedCamSpaceDiffFlags[flagPackOrder];
				// To test if there is a depth break in the current flag pack,
				// we must check all flags starting from the flagOrderInPack
				// downward to 0 position. We must skip all flags from
				// flagOrderInPack+1 to 31.
				if (flagOrderInPack < 31)
				{
					// Set all higher flags to 1, so that they will be skipped.
					// Note that if flagOrderInPack == 31, there are no flags to
					// skip. Note also that (U << 32) != 0 as it can be expected.
					// (U << 32) == U instead.
					flagPack |= ( uint(0xFFFFFFFFU) << uint(flagOrderInPack+1) );
				}
				// Find first zero flag starting from flagOrderInPack position.
				// Since all higher bits are set, they will be effectively skipped.
				int firstUnsetFlagPos = firstbithigh( uint(~flagPack) );
				// firstbithigh(0) == +INT_MAX
				if (!(0 <= firstUnsetFlagPos && firstUnsetFlagPos < 32))
					// There are no set flags => proceed to the next uint flag pack.
					firstUnsetFlagPos = -1;
				firstDepthBreakToTheLeftIndex -= flagOrderInPack - firstUnsetFlagPos;

#if INITIAL_SAMPLE_STEP > 32
				// Check the remaining full flag packs
				flagPackOrder--;
				while (flagPackOrder >= 0 && firstUnsetFlagPos == -1)
				{
					flagPack = packedCamSpaceDiffFlags[flagPackOrder];
					firstUnsetFlagPos = firstbithigh( uint(~flagPack) );
					if (!(0 <= firstUnsetFlagPos && firstUnsetFlagPos < 32))
						firstUnsetFlagPos = -1;
					firstDepthBreakToTheLeftIndex -= 31 - firstUnsetFlagPos;
					flagPackOrder--;
				}
#endif
				// Ray marching sample is located next to the identified depth break:
				leftSrcSampleIndex = max( uint(firstDepthBreakToTheLeftIndex + 1), initialSample0Index );
			}

			// Find right interpolation source using symmetric method
			{
				// We need to find first depth break starting from rightSrcSampleIndex
				// and going to the right up to the initialSample1Index.
				rightSrcSampleIndex = sampleIndex;
				int flagPackOrder = rightSrcSampleIndex / 32;
				uint flagOrderInPack = rightSrcSampleIndex % 32;
				uint flagPack = packedCamSpaceDiffFlags[flagPackOrder];
				// We need to find first unset flag in the current flag pack
				// starting from flagOrderinPack position and up to the 31st bit.
				// Set all lower order bits to 1 so that they are skipped during
				// the test:
				if (flagOrderInPack > 0)
					flagPack |= ( (1 << uint(flagOrderInPack)) - 1 );
				// Find first zero flag:
				int firstUnsetFlagPos = firstbitlow( uint(~flagPack) );
				if (!(0 <= firstUnsetFlagPos && firstUnsetFlagPos < 32))
					firstUnsetFlagPos = 32;
				rightSrcSampleIndex += firstUnsetFlagPos - flagOrderInPack;

#if INITIAL_SAMPLE_STEP > 32
				// Check the remaining full flag packs
				flagPackOrder++;
				while (flagPackOrder < int(gNumPackedFlags) && firstUnsetFlagPos == 32)
				{
					flagPack = packedCamSpaceDiffFlags[flagPackOrder];
					firstUnsetFlagpos = firstbitlow( uint(~flagPack) );
					if (!(0 <= firstUnsetFlagPos && firstUnsetFlagPos < 32))
						firstUnsetFlagPos = 32;
					rightsrcSampleIndex += firstUnsetFlagPos;
					flagPackOrder++;
				}
#endif
				rightSrcSampleIndex = min(rightSrcSampleIndex, initialSample1Index);
			}
		}

		// If at least one interpolation source is the same as the sample itself,
		// the sample is ray marching sample and is interpolated from itself:
		if (leftSrcSampleIndex == sampleIndex || rightSrcSampleIndex == sampleIndex)
			leftSrcSampleIndex = rightSrcSampleIndex = sampleIndex;
	}

	gInterpolationSource[ uint2(globalSampleIndex, sliceIndex) ] = 
		uint2(groupStartGlobalIndex + leftSrcSampleIndex, groupStartGlobalIndex + rightSrcSampleIndex);
}

technique11 RefineSampleLocations
{
	pass p0
	{
		SetVertexShader( NULL );
		SetGeometryShader( NULL );
		SetPixelShader( NULL );
		SetComputeShader( CompileShader( cs_5_0, RefineSampleLocationsCS() ) );
	}
}