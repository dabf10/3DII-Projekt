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

float4x4 gViewProjInv;
float4 gCameraPos;
float4 gDirOnLight;
float4 gLightWorldPos;
float4 gSpotLightAxisAndCosAngle;
float4x4 gWorldToLightProj;
float4 gCameraUVAndDepthInShadowMap;
#define LIGHT_TYPE_DIRECTIONAL 0
#define LIGHT_TYPE_SPOT 1
#define LIGHT_TYPE_POINT 2
#ifndef LIGHT_TYPE
#   define LIGHT_TYPE LIGHT_TYPE_DIRECTIONAL
#endif

Texture2D<float4> gSliceUVDirAndOrigin : register( t2 );
Texture2D<float> gLightSpaceDepthMap : register( t3 );
Texture2D<float2> gMinMaxLightSpaceDepth : register( t4 );
uint gSrcMinMaxLevelXOffset;
uint gDstMinMaxLevelXOffset;
float2 gShadowMapTexelSize;

#ifndef ANISOTROPIC_PHASE_FUNCTION
#   define ANISOTROPIC_PHASE_FUNCTION 1
#endif
#define PI 3.1415928f
Texture2D<uint2> gInterpolationSource : register( t7 );
Texture2D<float2> gCoordinates : register( t1 );
Texture2D<float3> gPrecomputedPointLightInsctr : register( t6 );
float gMaxTracingDistance;
float gMaxStepsAlongRay;
uint gMinMaxShadowMapResolution;
float gMaxShadowMapStep;
float4 gLightColorAndIntensity;
float4 gAngularRayleighBeta;
float4 gTotalRayleighBeta;
float4 gAngularMieBeta;
float4 gTotalMieBeta;
float4 gSummTotalBeta;
float4 gHG_g;

SamplerState gSamLinearClamp : register( s0 )
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

SamplerState gSamLinearBorder0 : register( s1 )
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Border;
	AddressV = Border;
	BorderColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
};

SamplerComparisonState gSamComparison : register( s3 )
{
	Filter = COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
	AddressU = Border;
	AddressV = Border;
	ComparisonFunc = GREATER;
	BorderColor = float4(0.0, 0.0, 0.0, 0.0);
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

float4 WorldSpaceToShadowMapUV(in float3 posWS)
{
	float4 lightProjSpacePos = mul( float4(posWS, 1), gWorldToLightProj );
	lightProjSpacePos.xyz /= lightProjSpacePos.w;
	float4 uvAndDepthInLightSpace;
	uvAndDepthInLightSpace.xy = ProjToUV( lightProjSpacePos.xy );
	// Applying depth bias results in light leaking through the opaque objects
	// when looking directly at the light source.
	uvAndDepthInLightSpace.z = lightProjSpacePos.z; // * gDepthBiasMultiplier;
	uvAndDepthInLightSpace.w = 1 / lightProjSpacePos.w;
	return uvAndDepthInLightSpace;
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

float3 ProjSpaceXYZToWorldSpace(in float3 posPS)
{
	// We need to compute depth before applying view-proj inverse matrix.
	float depth = gCameraProj[2][2] + gCameraProj[3][2] / posPS.z;
	float4 reconstructedPosWS = mul( float4(posPS.xy, depth, 1), gViewProjInv );
	reconstructedPosWS /= reconstructedPosWS.w;
	return reconstructedPosWS.xyz;
}

float3 ProjSpaceXYToWorldSpace(in float2 posPS)
{
	// We can sample camera space z texture using bilinear filtering.
	float camSpaceZ = gCamSpaceZ.SampleLevel(gSamLinearClamp, ProjToUV(posPS), 0);
	return ProjSpaceXYZToWorldSpace(float3(posPS, camSpaceZ));
}

bool PlanePlaneIntersect(float3 n1, float3 p1, float3 n2, float3 p2,
						 out float3 lineOrigin, out float3 lineDir)
{
	// http://paulbourke.net/geometry/planeplane/
	float d1 = dot(n1, p1);
	float d2 = dot(n2, p2);
	float n1n1 = dot(n1, n1);
	float n2n2 = dot(n2, n2);
	float n1n2 = dot(n1, n2);

	float det = n1n1 * n2n2 - n1n2 * n1n2;
	if (abs(det) < 1e-6)
		return false;

	float c1 = (d1 * n2n2 - d2 * n1n2) / det;
	float c2 = (d2 * n1n1 - d1 * n1n2) / det;

	lineOrigin = c1 * n1 + c2 * n2;
	lineDir = normalize(cross(n1, n2));

	return true;
}

float2 RayConeIntersect(in float3 coneApex, in float3 coneAxis, in float cosAngle,
						in float3 rayStart, in float3 rayDir)
{
	rayStart -= coneApex;
	float a = dot(rayDir, coneAxis);
	float b = dot(rayDir, rayDir);
	float c = dot(rayStart, coneAxis);
	float d = dot(rayStart, rayDir);
	float e = dot(rayStart, rayStart);
	cosAngle *= cosAngle;
	float A = a*a - b*cosAngle;
	float B = 2 * ( c*a - d*cosAngle );
	float C = c*c - e*cosAngle;
	float D = B*B - 4*A*C;
	if (D > 0)
	{
		D = sqrt(D);
		float2 t = (-B + sign(A)*float2(-D,+D)) / (2*A);
		bool2 isCorrect = c + a * t > 0;
		t = t * isCorrect + !isCorrect * (-FLT_MAX);
		return t;
	}
	else
		return -FLT_MAX;
}

static const float4 gIncorrectSliceUVDirAndStart = float4(-10000, -10000, 0, 0);
float4 RenderSliceUVDirectionPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	uint sliceIndex = input.PosH.x;

	// Load epipolar slice endpoints.
	float4 sliceEndpoints = gSliceEndPoints.Load( uint3(sliceIndex, 0, 0) );

	// All correct entry points are completely inside the [-1,1]x[-1,1] area.
	if (any(abs(sliceEndpoints.xy) > 1))
		return gIncorrectSliceUVDirAndStart;

	// Reconstruct slice exit point position in world space.
	float3 sliceExitWS = ProjSpaceXYToWorldSpace(sliceEndpoints.zw);
	float3 dirToSliceExitFromCamera = normalize(sliceExitWS - gCameraPos.xyz);
	
	// Compute epipolar slice normal. If light source is outside the screen, the
	// could be collinear.
	float3 sliceNormal = cross(dirToSliceExitFromCamera, gDirOnLight.xyz);
	if (length(sliceNormal) < 1e-5)
		return gIncorrectSliceUVDirAndStart;
	sliceNormal = normalize(sliceNormal);

	// Intersect epipolar slice plane with the light projection plane.
	float3 intersecOrig, intersecDir;

#if LIGHT_TYPE == LIGHT_TYPE_POINT || LIGHT_TYPE == LIGHT_TYPE_SPOT
	// We can use any plane parallel to the light frustum near clip plane. The
	// exact distance from the plane to light source does not matter since the
	// projection will always be the same:
	float3 lightProjPlaneCenter = gLightWorldPos.xyz + gSpotLightAxisAndCosAngle.xyz;
#endif

	if (!PlanePlaneIntersect(
#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL
		// In case light is directional, the matrix is not perspective, so location
		// of the light projection plane in space as well as camera position do not
		// matter at all.
		sliceNormal, 0,
		-gDirOnLight.xyz, 0,
#elif LIGHT_TYPE == LIGHT_TYPE_POINT || LIGHT_TYPE == LIGHT_TYPE_SPOT
		sliceNormal, gCameraPos.xyz,
		gSpotLightAxisAndCosAngle.xyz, lightProjPlaneCenter,
#endif
		intersecOrig, intersecDir ))
	{
		// There is no correct intersection between planes in barely possible case
		// which requires that:
		// 1. gDirOnLight is exactly parallel to light projection plane.
		// 2. The slice is parallel to light projection plane.
		return gIncorrectSliceUVDirAndStart;
	}

	// Important: Ray direction intersecDir is computed as a cross product of slice
	// normal and light direction (or spot light axis). As a result, the ray
	// direction is always correct for valid slices.

	// Now project the line onto the light space UV coordinates.
	// Get two points on the line:
	float4 p0 = float4( intersecOrig, 1 );
	float4 p1 = float4( intersecOrig + intersecDir * max(1, length(intersecOrig)), 1);

	// Transform the points into the shadow map UV:
	p0 = mul( p0, gWorldToLightProj );
	p0 /= p0.w;
	p1 = mul( p1, gWorldToLightProj);
	p1 /= p1.w;

	// Note that division by w is not really necessary because both points lie in
	// the plane parellel to light projection and thus have the same w value.
	float2 sliceDir = ProjToUV(p1.xy) - ProjToUV(p0.xy);

	// The followig method also works:
	// Since we need direction only, we can use any origin. The most convenient is
	// lightProjPlaneCenter which projects into (0.5, 0.5):
	// float4 sliceUVDir = mul( float4(lightProjPlaneCenter + intersecDir, 1), gWorldToLightProj);
	// sliceUVDir /= sliceUVDir.w;
	// float2 sliceDir = ProjToUV(sliceUVDir.xy) - 0.5;

	sliceDir /= max(abs(sliceDir.x), abs(sliceDir.y));

	float2 sliceOriginUV = gCameraUVAndDepthInShadowMap.xy;

#if LIGHT_TYPE == LIGHT_TYPE_POINT || LIGHT_TYPE == LIGHT_TYPE_SPOT
	bool isCamInsideCone = dot( -gDirOnLight.xyz, gSpotLightAxisAndCosAngle.xyz) > gSpotLightAxisAndCosAngle.w;
	if (!isCamInsideCone)
	{
		// If camera is outside the cone, all the rays in slice hit the same cone
		// side, which means that they all start from projection of this rib onto
		// the shadow map.

		// Intersect the ray with the light cone:
		float2 coneIsecs = 
			RayConeIntersect(gLightWorldPos.xyz, gSpotLightAxisAndCosAngle.xyz,
			gSpotLightAxisAndCosAngle.w, intersecOrig, intersecDir);

		if (any(coneIsecs == -FLT_MAX))
			return gIncorrectSliceUVDirAndStart;

		// Now select the first intersection with the cone along the ray.
		float4 rayConeIsec = float4(intersecOrig + min(coneIsecs.x, coneIsecs.y) * intersecDir, 1);

		// Project this intersection:
		rayConeIsec = mul( rayConeIsec, gWorldToLightProj );
		rayConeIsec /= rayConeIsec.w;

		sliceOriginUV = ProjToUV(rayConeIsec.xy);
	}
#endif

	return float4(sliceDir, sliceOriginUV);
}

technique11 RenderSliceUVDirection
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, RenderSliceUVDirectionPS() ) );
	}
}

// Note that min/max shadow map does not contain finest resolution level.
// The first level it contains corresponds to step == 2
float2 InitializeMinMaxShadowMapPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	uint sliceIndex = input.PosH.y;

	// Load slice direction in shadow map.
	float4 sliceUVDirAndOrigin = gSliceUVDirAndOrigin.Load( uint3( sliceIndex, 0, 0 ) );

	// Calculate current sample position on the ray.
	float2 currUV = sliceUVDirAndOrigin.zw + sliceUVDirAndOrigin.xy * floor(input.PosH.x) * 2.0f * gShadowMapTexelSize;

	// Gather 8 depths which will be used for PCF filtering for this sample and its
	// immediate neighbor along the epipolar slice.
	// Note that if the sample is located outside the shadow map, Gather() will
	// return 0 as specified by the samLinearBorder0. As a result, volumes outside
	// the shadow map will always be lit.
	float4 depths = gLightSpaceDepthMap.Gather(gSamLinearBorder0, currUV);

	// Shift UV to the next sample along the epipolar slice:
	currUV += sliceUVDirAndOrigin.xy * gShadowMapTexelSize;
	float4 neighbDepths = gLightSpaceDepthMap.Gather(gSamLinearBorder0, currUV);

	float4 minDepth = min(depths, neighbDepths);
	minDepth.xy = min(minDepth.xy, minDepth.zw);
	minDepth.x = min(minDepth.x, minDepth.y);

	float4 maxDepth = max(depths, neighbDepths);
	maxDepth.xy = max(maxDepth.xy, maxDepth.zw);
	maxDepth.x = max(maxDepth.x, maxDepth.y);

	return float2(minDepth.x, maxDepth.x);
}

technique11 InitializeMinMaxShadowMapLevel
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_5_0, InitializeMinMaxShadowMapPS() ) );
	}
}

// 1D min/max mip map is arranged as follows:
//
//		gSrcMinMaxLevelXOffset
//		 |
//		 |		gDstMinMaxLevelXOffset
//		 |		 |
//		 |_______|___ __
//		 |		 |	 |	|
//		 |		 |	 |	|
//		 |		 |	 |	|
//		 |		 |	 |	|
//		 |_______|___|__|
//		 |<----->|<->|
//			 |	   |
//			 |	 minMaxShadowMapResolution / 4?
//		minMaxShadowMapResolution / 2
float2 ComputeMinMaxShadowMapLevelPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	uint2 dstSampleIndex = uint2(input.PosH.xy);
	uint2 srcSample0Index = uint2(gSrcMinMaxLevelXOffset + (dstSampleIndex.x - gDstMinMaxLevelXOffset) * 2, dstSampleIndex.y);
	uint2 srcSample1Index = srcSample0Index + uint2(1, 0);

	float2 minMaxDepth0 = gMinMaxLightSpaceDepth.Load( uint3( srcSample0Index, 0 ) );
	float2 minMaxDepth1 = gMinMaxLightSpaceDepth.Load( uint3( srcSample1Index, 0 ) );

	float2 minMaxDepth;
	minMaxDepth.x = min(minMaxDepth0.x, minMaxDepth1.x);
	minMaxDepth.y = max(minMaxDepth0.y, minMaxDepth1.y);
	
	return minMaxDepth;
}

technique11 ComputeMinMaxShadowMapLevel
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, ComputeMinMaxShadowMapLevelPS() ) );
	}
}

void MarkRayMarchingSamplesInStencilPS( FullScreenTriangleVSOut input )
{
	uint2 interpolationSource = gInterpolationSource.Load( uint3( input.PosH.xy, 0 ) );
	// Ray marching samples are interpolated from themselves, so it is easy to
	// detect them:
	if (interpolationSource.x != interpolationSource.y)
		discard;
}

technique11 MarkRayMarchingSamplesInStencil
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, MarkRayMarchingSamplesInStencilPS() ) );
	}
}

float3 ApplyPhaseFunction(in float3 insctrIntegral, in float cosTheta)
{
	//    sun
    //      \
    //       \
    //    ----\------eye
    //         \theta 
    //          \
    //

	// Compute Rayleigh scattering Phase Function
	// According to formula for the Rayleigh Scattering phase function presented
	// in the "Rendering Outdoor Light Scattering in Real Time" by Hoffman and
	// Preetham, BethaR(Theta) is calculated as follows:
	// 3/(16PI) * BethaR * (1+cos^2(theta))
	// gAngularRayleighBeta == (3*PI/16) * gTotalRayleighBeta, hence:
	float3 RayleighScatteringPhaseFunc = gAngularRayleighBeta.rgb * (1.0 + cosTheta*cosTheta);

	// Compute Henyey-Greenstein approximation of the Mie scattering Phase Function.
	// According to formula for the Mie Scattering phase function presented in the
	// "Rendering Outdoor Light Scattering in Real Time" by Hoffman and Preetham,
	// BethaR(Theta) is calculated as follows:
	// 1/(4PI) * BethaM * (1-g^2)/(1+g^2-2g*cos(theta))^(3/2)
	// const float4 gHG_g = float4(1 - g*g, 1 + g*g, -2*g, 1);
	float HGTemp = rsqrt( dot(gHG_g.yz, float2(1.f, cosTheta)) );
	// gAngularMieBeta is calculated according to formula presented in "A practical
	// Analytic Model for Daylight" by Preetham & Hoffman
	float3 mieScatteringPhaseFunc_HGApprox = gAngularMieBeta.rgb * gHG_g.x * (HGTemp*HGTemp*HGTemp);

	float3 inscatteredLight = insctrIntegral * (RayleighScatteringPhaseFunc + mieScatteringPhaseFunc_HGApprox);

	inscatteredLight.rgb *= gLightColorAndIntensity.w;

	return inscatteredLight;
}

float3 CalculateInscattering( in float2 rayMarchingSampleLocation,
							  in uniform const bool applyPhaseFunction = false,
							  in uniform const bool use1DMinMaxMipMap = false,
							  uint epipolarSliceIndex = 0 )
{
	float3 reconstructedPos = ProjSpaceXYToWorldSpace( rayMarchingSampleLocation );

	float3 rayStartPos = gCameraPos.xyz;
	float3 rayEndPos = reconstructedPos;
	float3 eyeVector = rayEndPos.xyz - rayStartPos;
	float traceLength = length(eyeVector);
	eyeVector /= traceLength;

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL
	// Update end position
	traceLength = min(traceLength, gMaxTracingDistance);
	rayEndPos = gCameraPos.xyz + traceLength * eyeVector;
#elif LIGHT_TYPE == LIGHT_TYPE_POINT || LIGHT_TYPE == LIGHT_TYPE_SPOT

	//					Light
	//					  *					-
	//				   .' |\				|
	//				 .'	  | \				| closestDistToLight
	//			   .'	  |  \				|
	//			 .'		  |   \				|
	//		Cam *--------------*------->	-
	//			|<------->|     \
	//				\
	//			startDistFromProjection

	float distToLight = length( gLightWorldPos.xyz - gCameraPos.xyz );
	float cosLV = dot( gDirOnLight.xyz, eyeVector );
	float distToClosestToLightPoint = distToLight * cosLV;
	float closestDistToLight = distToLight * sqrt(1 - cosLV * cosLV);
	float v = closestDistToLight / gMaxTracingDistance;

	float3 closestPointToLight = gCameraPos.xyz + eyeVector * distToClosestToLightPoint;

	float cameraU = GetPrecomputedPtLghtSrcTexU( gCameraPos.xyz, eyeVector, closestPointToLight );
	float reconstrPointU = GetPrecomputedPtLghtSrcTexU( reconstructedPos, eyeVector, closestPointToLight );

	float3 cameraInsctrIntegral = gPrecomputedPointLightInsctr.SampleLevel( gSamLinearClamp, float2(cameraU, v), 0);
	float3 rayTerminationInsctrIntegral = exp(-traceLength * gSummTotalBeta.rgb) * gPrecomputedPointLightInsctr.SampleLevel( gSamLinearClamp, float2( reconstrPointU, v), 0 );

	float3 fullyLitInsctrIntegral = (cameraInsctrIntegral - rayTerminationInsctrIntegral) *
									gLightColorAndIntensity.rgb * gLightColorAndIntensity.w;

	bool isCamInsideCone = dot( -gDirOnLight.xyz, gSpotLightAxisAndCosAngle.xyz) > gSpotLightAxisAndCosAngle.w;

	// Eye rays directed at exactly the light source requires special handling.
	if (cosLV > 1 - 1e-6)
	{
		float isInLight = bIsCamInsideCone ?
			gLightSpaceDepthMap.SampleCmpLevelZero( gSamComparison, gCameraUVAndDepthInShadowMap.xy, gCameraUVAndDepthInShadowMap.z ).x :
			1;

		// This term is required to eliminate bright point visible through scene
		// geometry when the camera is outside the light cone.
		float isLightVisible = bIsCamInsideCone || (distToLight < traceLength);
		return fullyLitInsctrIntegral * isInLight * isLightVisible;
	}

	float startDistance;
	TruncateEyeRayToLightCone( eyeVector, rayStartPos, rayEndPos, traceLength, startDistance, isCamInsideCone );

#endif

	// If tracing distance is very short, we can fall into an infinite loop due
	// to 0 length step and crash the driver. Return from function in this case.
	if (traceLength < gMaxTracingDistance * 0.0001)
	{
#if LIGHT_TYPE == LIGHT_TYPE_POINT
		return fullyLitInsctrIntegral;
#else
		return float3(0,0,0);
#endif
	}

	// We trace the ray not in the world space, but in the light projection space.

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL
	// Get start and end positions of the ray in the light projection space
	float4 startUVAndDepthInLightSpace = float4(gCameraUVAndDepthInShadowMap.xyz, 1);
#elif LIGHT_TYPE == LIGHT_TYPE_POINT || LIGHT_TYPE_SPOT
	float4 startUVAndDepthInLightSpace = WorldSpaceToShadowMapUV( rayStartPos );
#endif

	// Compute shadow map UV coordinates of the ray end point and its depth in the
	// light space.
	float4 endUVAndDepthInLightSpace = WorldSpaceToShadowMapUV( rayEndPos );

	// Calculate normalized trace direction in the light projection space and its length
	float3 shadowMapTraceDir = endUVAndDepthInLightSpace.xyz - startUVAndDepthInLightSpace.xyz;

	// If the ray is directed exactly at the light source, trace length will be zero.
	// Clamp to a very small positive value to avoid division by zero.
	// Also assure that trace length is not longer than maximum meaningful length.
	float traceLengthInShadowMapUVSpace = clamp( length( shadowMapTraceDir.xy ), 1e-6, sqrt(2.f) );
	shadowMapTraceDir /= traceLengthInShadowMapUVSpace;

	float shadowMapUVStepLength = 0;
	float2 sliceOriginUV = 0;
	if (use1DMinMaxMipMap)
	{
		// Get UV direction for this slice
		float4 sliceUVDirAndOrigin = gSliceUVDirAndOrigin.Load( uint3(epipolarSliceIndex, 0, 0) );
		if (all(sliceUVDirAndOrigin == gIncorrectSliceUVDirAndStart))
		{
#if LIGHT_TYPE == LIGHT_TYPE_POINT
			return fullyLitInsctrIntegral;
#else
			return float3(0,0,0);
#endif
		}

		// Scale with the shadow map texel size.
		shadowMapUVStepLength = length(sliceUVDirAndOrigin.xy * gShadowMapTexelSize);
		sliceOriginUV = sliceUVDirAndOrigin.zw;
	}
	else
	{
		// Calculate length of the trace step in light projection space.
		shadowMapUVStepLength = gShadowMapTexelSize.x / max(abs(shadowMapTraceDir.x), abs(shadowMapTraceDir.y));

		// Take into account maximum number of steps specified by gMaxStepsAlongRay
		shadowMapUVStepLength = max(traceLengthInShadowMapUVSpace / gMaxStepsAlongRay, shadowMapUVStepLength);
	}

	// Calculate ray step length in world space.
	float rayStepLengthWS = traceLength * (shadowMapUVStepLength / traceLengthInShadowMapUVSpace);

	// Assure that step length is not 0 so that we will not fall into an infinite
	// loop and will not crash the driver.
	//rayStepLengthWS = max(rayStepLengthWS, gMaxTracingDistance * 1e-5);

	// Scale trace direction in light projection space to calculate the final step.
	float3 shadowMapUVAndDepthStep = shadowMapTraceDir * shadowMapUVStepLength;

	float3 inScatteringIntegral = 0;
	float3 prevInsctrIntegralValue = 1; // exp( -0 * gSummTotalBeta.rgb );
	// March the ray
	float totalMarchedDistance = 0;
	float totalMarchedDistInUVSpace = 0;
	float3 currShadowMapUVAndDepthInLightSpace = startUVAndDepthInLightSpace.xyz;

	// The following variables are used only if 1D min map optimization is enabled.
	uint minLevel = 0; // max( log2( (traceLengthInShadowMapUVSpace / shadowMapUVStepLength) / gMaxStepsAlongRay), 0);
	uint currSamplePos = 0;

	// For spot light, the slice start UV is either location of camera in light proj
	// space or intersection of the slice with the cone rib. No adjustment is
	// required in either case.
#if LIGHT_TYPE == LIGHT_TYPE_POINT || LIGHT_TYPE == LIGHT_TYPE_SPOT
	float insctrTexStartU = GetPrecomputedPtLghtSrcTexU( rayStartPos, eyeVector, closestPointToLight );
	float insctrTexEndU = GetPrecomputedPtLghtSrcTexU( rayEndPos, eyeVector, closestPointToLight );

#	if LIGHT_TYPE == LIGHT_TYPE_POINT
	// Add inscattering contribution outside the light cone.
	inScatteringIntegral = (cameraInsctrIntegral - prevInsctrIntegralValue + exp(-(startDistance + traceLength) * gSummTotalBeta.rgb) * gPrecomputedPointLightInsctr.SampleLevel(gSamLinearClamp, float2(insctrTexEndU, v), 0) - rayTerminationInsctrIntegral ) * gLightColorAndIntensity.rgb;
#	endif
#endif

	uint currTreeLevel = 0;
	// Note that min/max shadow map does not contain finest resolution level.
	// The first level it contains corresponds to step == 2
	int levelDataOffset = -int(gMinMaxShadowMapResolution);
	float step = 1.f;
	float maxShadowMapStep = gMaxShadowMapStep;

	[loop]
	while (totalMarchedDistInUVSpace < traceLengthInShadowMapUVSpace)
	{
		// Clamp depth to a very small positive value to not let the shadow rays
		// get clipped at the shadow map far clipping plane.
		float currDepthInLightSpace = max(currShadowMapUVAndDepthInLightSpace.z, 1e-7);
		float isInLight = 0;

		if (use1DMinMaxMipMap)
		{
			// If the step is smaller than the maximum allowed and the sample is
			// located at the appropriate position, advance to the next coarser level.
			if (step < maxShadowMapStep && ((currSamplePos & ((2 << currTreeLevel) - 1)) == 0))
			{
				levelDataOffset += gMinMaxShadowMapResolution >> currTreeLevel;
				currTreeLevel++;
				step *= 2.f;
			}

			while (currTreeLevel > minLevel)
			{
				// Compute light space depths at the ends of the current ray section
				
				// What we need here is actually depth which is divided by the
				// camera view space z. Thus depth can be correctly interpolated
				// in screen space:
                // http://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf
				// A subtle moment here is that we need to be sure that we can skip
				// step samples starting from 0 up to step-1. We do not need to do
				// any checks against the sample step away:
				//
                //     --------------->
                //
                //          *
                //               *         *
                //     *              *     
                //     0    1    2    3
                //
                //     |------------------>|
                //           step = 4
				float nextLightSpaceDepth = currShadowMapUVAndDepthInLightSpace.z + shadowMapUVAndDepthStep.z * (step-1);
				float2 startEndDepthOnRaySection = float2(currShadowMapUVAndDepthInLightSpace.z, nextLightSpaceDepth);
				startEndDepthOnRaySection = max(startEndDepthOnRaySection, 1e-7);

				// Load 1D min/max depths.
				float4 currMinMaxDepth = gMinMaxLightSpaceDepth.Load( uint3( (currSamplePos >> currTreeLevel) + levelDataOffset, epipolarSliceIndex, 0) ).xyxy;

				isInLight = all( startEndDepthOnRaySection >= currMinMaxDepth.yw );

				bool isInShadow = all( startEndDepthOnRaySection < currMinMaxDepth.xz );

				if (isInLight || isInShadow)
					// If the ray section is fully lit or shadow, we can break the loop.
					break;
				// If the ray section is neither fully lit nor shadowed, we have to
				// go to the finer level.
				currTreeLevel--;
				levelDataOffset -= gMinMaxShadowMapResolution >> currTreeLevel;
				step /= 2.f;
			};

			// If we are at the finest level, sample the shadow map with PCF
			[branch]
			if (currTreeLevel <= minLevel)
			{
				isInLight = gLightSpaceDepthMap.SampleCmpLevelZero( gSamComparison, currShadowMapUVAndDepthInLightSpace.xy, currDepthInLightSpace ).x;
			}
		}
		else
		{
			isInLight = gLightSpaceDepthMap.SampleCmpLevelZero( gSamComparison, currShadowMapUVAndDepthInLightSpace.xy, currDepthInLightSpace ).x;
		}

		float3 lightColorInCurrPoint = gLightColorAndIntensity.rgb;

		currShadowMapUVAndDepthInLightSpace += shadowMapUVAndDepthStep * step;
		totalMarchedDistInUVSpace += shadowMapUVStepLength * step;
		currSamplePos += 1 << currTreeLevel; // int -> float conversions are slow

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL
		totalMarchedDistance += rayStepLengthWS * step;
		float integrationDist = min(totalMarchedDistance, traceLength);

		// Calculate inscattering integral from the camera to the current point
		// analytically:
		float3 currInscatteringIntegralValue = exp( -integrationDist * gSummTotalBeta.rgb );
#elif LIGHT_TYPE == LIGHT_TYPE_SPOT || LIGHT_TYPE == LIGHT_TYPE_POINT
		// http://www.comp.nus.edu.sg/~lowkl/publications/lowk_persp_interp_techrep.pdf
        // An attribute A itself cannot be correctly interpolated in screen space.
		// However, A/z where z is the camera view space coordinate, does interpolate
		// correctly. 1/z also interpolates correctly, thus to properly interpolate
		// A it is necessary to do the following: lerp(A/z) / lerp(1/z)
		// Note that since eye ray directed at exactly the light source is handled
		// separately, camera space z can never become zero.
		float relativePos = saturate(totalMarchedDistInUVSpace / traceLengthInShadowMapUVSpace);
		float currW = lerp(startUVAndDepthInLightSpace.w, endUVAndDepthInLightSpace.w, relativePos);
		float distFromCamera = lerp(startDistance * startUVAndDepthInLightSpace.w, (startDistance + traceLength) * endUVAndDepthInLightSpace.w, relativePos) / currW;
		float currU = lerp(insctrTexStartU * startUVAndDepthInLightSpace.w, insctrTexEndU * endUVAndDepthInLightSpace.w, relativePos) / currW;
		float3 currInscatteringIntegralValue = exp(-distFromCamera * gSummTotalBeta.rgb) * gPrecomputedPointLightInsctr.SampleLevel(gSamLinearClamp, float2(currU, v), 0);
#endif

		float3 scatteredLight;
		// scatteredLight contains correct scattering light value with respect to
		// extinction.
		scatteredLight.rgb = (prevInsctrIntegralValue.rgb - currInscatteringIntegralValue.rgb) * isInLight;
		scatteredLight.rgb *= lightColorInCurrPoint;
		inScatteringIntegral.rgb += scatteredLight.rgb;

		prevInsctrIntegralValue.rgb = currInscatteringIntegralValue.rgb;
	}

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL
	inScatteringIntegral = inScatteringIntegral / gSummTotalBeta.rgb;
#else
	inScatteringIntegral = gLightColorAndIntensity.w;
#endif

#if LIGHT_TYPE == LIGHT_TYPE_DIRECTIONAL
	if (applyPhaseFunction)
		return ApplyPhaseFunction(inScatteringIntegral, dot(eyeVector, gDirOnLight.xyz));
	else
#endif
		return inScatteringIntegral;
}

float3 RayMarchMinMaxOptPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	uint2 samplePosSliceIndex = uint2( input.PosH.xy );
	float2 sampleLocation = gCoordinates.Load( uint3( samplePosSliceIndex, 0 ) );

	[branch]
	if (any(abs(sampleLocation) > 1+1e-3))
		return 0;

	return CalculateInscattering(sampleLocation,
		false, // Don't apply phase function
		true, // Use min/max optimization
		samplePosSliceIndex.y);
}

technique11 DoRayMarch
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, RayMarchMinMaxOptPS() ) );
	}
}

float3 EvaluatePhaseFunction(float cosTheta)
{
#if ANISOTROPIC_PHASE_FUNCTION
	float3 rlghInsctr = gAngularRayleighBeta.rgb * (1.0 * cosTheta*cosTheta);
	float HGTemp = rsqrt( dot(gHG_g.yz, float2(1.f, cosTheta)) );
	float3 mieInsctr = gAngularMieBeta.rgb * gHG_g.x * (HGTemp*HGTemp*HGTemp);
#else
	float3 rlghInsctr = gTotalRayleighBeta.rgb / (4.0*PI);
	float3 mieInsctr = gTotalMieBeta.rgb / (4.0*PI);
#endif

	return rlghInsctr + mieInsctr;
}

float3 PrecomputePointLightInsctrPS( FullScreenTriangleVSOut input ) : SV_TARGET
{
	float maxTracingDistance = gMaxTracingDistance;
	//                       Light
    //                        *                   -
    //                     .' |\                  |
    //                   .'   | \                 | closestDistToLight
    //                 .'     |  \                |
    //               .'       |   \               |
    //          Cam *--------------*--------->    -
    //              |<--------|     \
    //                  \
    //                  startDistFromProjection
    //
	float2 uv = input.TexC;
	float startDistFromProjection = input.PosH.x * maxTracingDistance;
	float closestDistToLight = uv.y * maxTracingDistance;

	float3 insctrRadiance = 0;

	// There is a very important property: pre-computed scattering must be
	// monotonical with respect to u coordinate. However, if we simply subdivide
	// the tracing distance onto the equal number of steps as in the following
	// code, we cannot guarantee this.
	//
	// float stepWorldLen = length(startPos - endPos) / numSteps;
	// for (float relativePos = 0; relativePos < 1; relativePos += 1.f / numSteps)
	// {
	//		float2 currPos = lerp(startPos, endPos, relativePos);
	//		...
	//
	// To assure that the scattering is monotonically increasing, we must go through
	// exactly the same taps for all pre-computations. The simple method to achieve
	// this is to make the world step the same as the difference between two
	// neighboring texels: The step can also be integral part of it, but not
	// greater! So /2 will work, but *2 won't!
	float stepWorldLen = ddx(startDistFromProjection);
	for (float distFromProj = startDistFromProjection; distFromProj < maxTracingDistance; distFromProj += stepWorldLen)
	{
		float2 currPos = float2(distFromProj, -closestDistToLight);
		float distToLightSqr = dot(currPos, currPos);
		float distToLight = sqrt(distToLightSqr);
		float distToCam = currPos.x - startDistFromProjection;
		float3 extinction = exp( -(distToCam + distToLight) * gSummTotalBeta.rgb );
		float2 lightDir = normalize(currPos);
		float cosTheta = -lightDir.x;

		float3 dLInsctr = extinction * EvaluatePhaseFunction(cosTheta) * stepWorldLen / max(distToLightSqr, maxTracingDistance * maxTracingDistance * 1e-8);
		insctrRadiance += dLInsctr;
	}

	return insctrRadiance;
}

technique11 PrecomputePointLightInsctr
{
	pass p0
	{
		SetVertexShader( CompileShader( vs_4_0, FullScreenTriangleVS() ) );
		SetGeometryShader( NULL );
		SetPixelShader( CompileShader( ps_4_0, PrecomputePointLightInsctrPS() ) );
	}
}