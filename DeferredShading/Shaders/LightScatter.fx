Texture2D<float> gSceneDepth : register( t0 );
float4x4 gCameraProj;

uint gNumEpipolarSlices;
float2 gScreenResolution;
//bool gIsLightOnScreen;
float4 gLightScreenPos;
#define FLT_MAX 3.402823466e+38f
uint gMaxSamplesInSlice;

Texture2D<float> gCamSpaceZ : register( t0 );
Texture2D<float4> gSliceEndPoints : register( t4 );

SamplerState gSamLinearClamp : register( s0 )
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

struct FullScreenTriangleVSOut
{
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD;
};

FullScreenTriangleVSOut FullScreenTriangleVS( uint VertexID : SV_VertexID )
{
	FullScreenTriangleVSOut output = (FullScreenTriangleVSOut)0;

	output.PosH.x = (VertexID == 2) ? 3.0f : -1.0f;
	output.PosH.y = (VertexID == 0) ? -3.0f : 1.0f;
	output.PosH.zw = 1.0f;

	output.TexC = output.PosH.xy * float2(0.5f, -0.5f) + 0.5f;

	return output;
}

float ReconstructCameraSpaceZPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	float depth = gSceneDepth.Load( uint3( input.PosH.xy, 0 ) );
	float camSpaceZ = gCameraProj[3][2] / (depth - gCameraProj[2][2]);
	return camSpaceZ;
}

technique11 ReconstructCameraSpaceZ
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, ReconstructCameraSpaceZPS() ) );
	}
}

float2 ProjToUV( in float2 projSpaceXY )
{
	return float2(0.5, 0.5) + float2(0.5, -0.5) * projSpaceXY;
}

const float4 GetOutermostScreenPixelCoords( )
{
	// The outermost visible screen pixels centers do not lie exactly on the boundary
	// (+1 or -1), but are biased by 0.5 screen pixel size inwards.
	return float4(-1, -1, 1, 1) + float4(1, 1, -1, -1) / gScreenResolution.xyxy;
}

float2 GetEpipolarLineEntryPoint(float2 exitPoint)
{
	float2 entryPoint;
	
	//if (gIsLightOnScreen)
	if (all(abs(gLightScreenPos.xy) <= 1))
	{
		// If light source is inside the screen, its location is entry point for
		// every epipolar line.
		entryPoint = gLightScreenPos.xy;
	}
	else
	{
		// If light source is outside the screen, we need to compute intersection
		// of the ray with the screen boundaries.

		// Compute direction from the light source to the exit point.
		// Note that exit point must be located on shrinked screen boundary.
		float2 rayDir = exitPoint.xy - gLightScreenPos.xy;
		float distToExitBoundary = length(rayDir);
		rayDir /= distToExitBoundary;

		// Compute signed distances along the ray from the light position to all
		// four boundaries. The distances are computed as follows using vector
		// instructions:
		// float distToLeftBoundary		= abs(rayDir.x) > 1e-5 ? (-1 - gLightScreenPos.x) / rayDir.x : -FLT_MAX;
		// float distToBottomBoundary	= abs(rayDir.y) > 1e-5 ? (-1 - gLightScreenPos.y) / rayDir.y : -FLT_MAX;
		// float distToRightBoundary	= abs(rayDir.x) > 1e-5 ? ( 1 - gLightScreenPos.x) / rayDir.x : -FLT_MAX;
		// float distToTopBoundary		= abs(rayDir.y) > 1e-5 ? ( 1 - gLightScreenPos.y) / rayDir.y : -FLT_MAX;

		// Note that in fact the outermost visible screen pixels do not lie exactly
		// on the boundary (+1 or -1), but are biased by 0.5 screen pixel size
		// inwards. Using these adjusted boundaries improves precision and results
		// in smaller number of pixels which require inscattering correction.
		float4 boundaries = GetOutermostScreenPixelCoords();
		bool4 isCorrectIntersectionFlag = abs(rayDir.xyxy) > 1e-5;
		float4 distToBoundaries = (boundaries - gLightScreenPos.xyxy) / (rayDir.xyxy + !isCorrectIntersectionFlag);
		// Addition of !isCorrectIntersectionFlag is required to prevent division by
		// zero. Note that such incorrect lanes will be masked out anyway.

		// We now need to find first intersection BEFORE the intersection with the
		// exit boundary. This means that we need to find maximum intersection
		// distance which is less than distToBoundary. We thus need to skip all
		// boundaries, distance to which is greater than the distance to exit
		// boundary. Using -FLT_MAX as the distance to these boundaries will result
		// in skipping them.
		isCorrectIntersectionFlag = isCorrectIntersectionFlag && ( distToBoundaries < (distToExitBoundary - 1e-4) );
		distToBoundaries = isCorrectIntersectionFlag * distToBoundaries +
			!isCorrectIntersectionFlag * float4(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);

		float firstIntersectionDist = 0;
		firstIntersectionDist = max(firstIntersectionDist, distToBoundaries.x);
		firstIntersectionDist = max(firstIntersectionDist, distToBoundaries.y);
		firstIntersectionDist = max(firstIntersectionDist, distToBoundaries.z);
		firstIntersectionDist = max(firstIntersectionDist, distToBoundaries.w);

		// The code above is equivalent to the following lines:
		// firstIntersectionDist = distToLeftBoundary	< distToBoundary - 1e-4 ? max(firstIntersectionDist, distToLeftBoundary		: firstIntersectionDist;
		// firstIntersectionDist = distToBottomBoundary	< distToBoundary - 1e-4 ? max(firstIntersectionDist, distToBottomBoundary	: firstIntersectionDist;
		// firstIntersectionDist = distToRightBoundary	< distToBoundary - 1e-4 ? max(firstIntersectionDist, distToRightBoundary	: firstIntersectionDist;
		// firstIntersectionDist = distToTopBoundary	< distToBoundary - 1e-4 ? max(firstIntersectionDist, distToTopBoundary		: firstIntersectionDist;

		// Now we can compute entry point.
		entryPoint = gLightScreenPos.xy + rayDir * firstIntersectionDist;

        // For invalid rays, coordinates are outside [-1,1]x[-1,1] area and such
		// rays will be discarded.
        //
        //       gLightScreenPos
        //             *
        //              \|
        //               \-entryPoint
        //               |\
        //               | \  exitPoint 
        //               |__\/___
        //               |       |
        //               |       |
        //               |_______|
        //
	}

	return entryPoint;
}

float4 GenerateSliceEndpointsPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	//float2 uv = ProjToUV( input.TexC );
	float2 uv = input.TexC;

	// Offset a half texel. Also clamp to [0,1] range.
	float epipolarSlice = saturate( uv.x - 0.5f / (float)gNumEpipolarSlices );

	// epipolarSlice lies in the range [0, 1 - 1 / gNumEpipolarSlices]
	// 0 defines location in exactly top left corner, 1 - 1 / gNumEpipolarSlices
	// defines position on the top boundary next to the top left corner.
	uint boundary = clamp(floor( epipolarSlice * 4 ), 0, 3);
	float posOnBoundary = frac( epipolarSlice * 4 );

    //             <------
    //   +1   0,1___________0.75
    //          |     3     |
    //        | |           | A
    //        | |0         2| |
    //        V |           | |
    //   -1     |_____1_____|
    //       0.25  ------>  0.5
    //
    //         -1          +1
    //

	//									Left			Bottom				Right					Top
	float4 boundaryXPos = float4(			0		, posOnBoundary	,		1			,	1 - posOnBoundary	);
	float4 boundaryYPos = float4( 1 - posOnBoundary	,		0		,	posOnBoundary	,			1			);
	bool4 boundaryFlags = bool4( boundary.xxxx == uint4(0, 1, 2, 3) );

	// Select the right coordinates for the boundary
	float2 exitPointPosOnBoundary = float2( dot(boundaryXPos, boundaryFlags), dot(boundaryYPos, boundaryFlags) );

	// Note that in fact the outermost visible screen pixels do not lie exactly on
	// the boundary (+1 or -1), but are biased by 0.5 screen pixel size inwards.
	// Using these adjusted boundaries improves precision and results in smaller
	// number of pixels which require inscattering correction.
	float4 outermostScreenPixelCoords = GetOutermostScreenPixelCoords(); // xyzw = (left, bottom, right, top)
	float2 exitPoint = lerp( outermostScreenPixelCoords.xy, outermostScreenPixelCoords.zw, exitPointPosOnBoundary);

	// GetEpipolarLineEntryPoint() gets exit point on SHRINKED boundary
	float2 entryPoint = GetEpipolarLineEntryPoint( exitPoint );

	// Last step optimizes sample locations.
	// If epipolar slice is not invisible, advance its exit point if necessary.
	// Recall that all correct entry points are completely inside the [-1,1]x[-1,1] area.
	if ( all(abs(entryPoint) < 1) )
	{
		// Compute length of the epipolar line in screen pixels.
		float epipolarSliceScreenLength = length( (exitPoint - entryPoint) * gScreenResolution.xy / 2 );

		// If epipolar line is too short, update epipolar line exit point to provide
		// 1:1 texel to screen pixel correspondence.
		exitPoint = entryPoint + (exitPoint - entryPoint) * max((float)gMaxSamplesInSlice / epipolarSliceScreenLength, 1);
	}

	return float4(entryPoint, exitPoint);
}

technique11 GenerateSliceEndpoints
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, GenerateSliceEndpointsPS() ) );
	}
}

float GetCamSpaceZ(in float2 screenSpaceUV)
{
	return gCamSpaceZ.SampleLevel( gSamLinearClamp, screenSpaceUV, 0 );
}

void GenerateCoordinateTexturePS( FullScreenTriangleVSOut input,
								 out float2 xy : SV_TARGET0,
								 out float camSpaceZ : SV_TARGET1)
{
	float4 sliceEndPoints = gSliceEndPoints.Load( int3(input.PosH.y, 0, 0) );

	// If slice entry point is outside [-1,1]x[-1,1] area, the slice is completely
	// invisible and we can skip it from further processing. Note that slice exit
	// point can lie outside the screen, if sample locations are optimized.
	// Recall that all correct entry points are completely inside [-1,1]x[-1,1].
	if (any(abs(sliceEndPoints.xy) > 1))
	{
		// Discard invalid slices, such slices will not be marked in the stencil
		// and as a result will always be skipped.
		discard;
	}

	//float2 uv = ProjToUV(input.TexC);
	float2 uv = input.TexC;

	// Offset UV coordinates by half a texel.
	float samplePosOnEpipolarLine = uv.x - 0.5f / (float)gMaxSamplesInSlice;

	// samplePosOnEpipolarLine is now in the range [0, 1 - 1 / gMaxSamplesInSlice].
	// We need to rescale it to [0,1].
	samplePosOnEpipolarLine *= (float)gMaxSamplesInSlice / ((float)gMaxSamplesInSlice - 1.0f);
	samplePosOnEpipolarLine = saturate(samplePosOnEpipolarLine);

	// Compute interpolated position between entry and exit points.
	xy = lerp(sliceEndPoints.xy, sliceEndPoints.zw, samplePosOnEpipolarLine);

	// All correct entry points are completely inside the [-1,1]x[-1,1] area.
	if (any(abs(xy) > 1))
	{
		// Discard pixels that fall behind the screen. This can happen if slice
		// exit point was optimized.
		discard;
	}

	// Compute camera space z for current location.
	camSpaceZ = GetCamSpaceZ( ProjToUV( xy ) );
}

technique11 GenerateCoordinateTexture
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, GenerateCoordinateTexturePS() ) );
	}
}

