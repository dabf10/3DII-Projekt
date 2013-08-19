Texture2D gCompositeImage;

SamplerState gImageSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = CLAMP;
	AddressV = CLAMP;
};

float gSepiaValue;
float gNoiseValue;
float gScratchValue;
float gInnerVignetting;
float gOuterVignetting;
float gRandomValue;
float gTimeLapse;

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

// Computes the overlay between the source and destination colors.
float3 Overlay( float3 src, float3 dst )
{
	// if (dst <= omega) then: 2 * src * dst
	// if (dst > omega) then: 1 - 2 * (1-dst) * (1-src)
	return float3((dst.x <= 0.5) ? (2.0 * src.x * dst.x) : (1.0 - 2.0 * (1.0 - dst.x) * (1.0 - src.x)),
				  (dst.y <= 0.5) ? (2.0 * src.y * dst.y) : (1.0 - 2.0 * (1.0 - dst.y) * (1.0 - src.y)),
				  (dst.z <= 0.5) ? (2.0 * src.z * dst.z) : (1.0 - 2.0 * (1.0 - dst.z) * (1.0 - src.z)));
}

// 2D Noise by Ian McEwan, Ashima Arts.
float3 mod289( float3 x ) { return x - floor(x * (1.0f / 289.0f)) * 289.0f; }
float2 mod289( float2 x ) { return x - floor(x * (1.0f / 289.0f)) * 289.0f; }
float3 permute( float3 x ) { return mod289(((x*34.0f)+1.0f)*x); }
float snoise( float2 v )
{
	const float4 C = float4(0.211324865405187, // (3.0-sqrt(3.0))/6.0
							0.366025403784439, // 0.5*(sqrt(3.0)-1.0)
							-0.577350269189626, // -1.0 + 2.0 * C.x
							0.024390243902439); // 1.0 / 41.0

	// First corner
	float2 i = floor(v + dot(v, C.yy));
	float2 x0 = v -  i + dot(i, C.xx);

	// Other corners
	float2 i1;
	i1 = (x0.x > x0.y) ? float2(1.0f, 0.0f) : float2(0.0f, 1.0f);
	float4 x12 = x0.xyxy + C.xxzz;
	x12.xy -= i1;

	// Permutations
	i = mod289(i); // Avoid truncation effects in permutation
	float3 p = permute( permute( i.y + float3(0.0f, i1.y, 1.0f ))
		+ i.x + float3(0.0f, i1.x, 1.0f ));

	float3 m = max(0.5f - float3(dot(x0, x0), dot(x12.xy, x12.xy), dot(x12.zw, x12.zw)), 0.0f);
	m = m*m;
	m = m*m;

	// Gradients: 41 points uniformly over a line, mapped onto a diamond.
	// The ring size 17*17 = 289 is close to a multiple of 41 (41*7 = 287)

	float3 x = 2.0f * frac(p * C.www) - 1.0f;
	float3 h = abs(x) - 0.5f;
	float3 ox = floor(x + 0.5f);
	float3 a0 = x - ox;

	// Normalize gradients implicitly by scaling m
	// Approximation of: m *= inversesqrt( a0*a0 + h*h );
	m *= 1.79284291400159 - 0.85373472095314 * ( a0*a0 + h*h );

	// Compute final noise value at P
	float3 g;
	g.x = a0.x * x0.x + h.x * x0.y;
	g.yz = a0.yz * x12.xz + h.yz * x12.yw;
	return 130.0f * dot(m, g);
}

float4 PS( VS_OUT input ) : SV_TARGET
{
	// Sepia RGB value
	float3 sepia = float3(112.0f / 255.0f, 66.0f / 255.0f, 20.0f / 255.0f);

	// Step 1: Convert to grayscale
	float3 color = gCompositeImage.Sample( gImageSampler, input.TexC ).xyz;
	float gray = (color.x, color.y, color.z) / 3.0f;
	float3 grayscale = float3(gray, gray, gray);

	// Step 2: Apply sepia overlay
	float3 finalColor = Overlay(sepia, grayscale);

	// Step 3: Lerp final sepia color
	finalColor = grayscale + gSepiaValue * (finalColor - grayscale);

	// Step 4: Add noise
	float noise = snoise( input.TexC * float2(1024.0f + gRandomValue * 512.0f, 1024.0f + gRandomValue * 512.0f)) * 0.5f;
	finalColor += noise * gNoiseValue;

	// Optionally add noise as an overlay, simulating ISO on the camera
	// float3 noiseOverlay = Overlay(finalColor, float3(noise));
	// finalColor = finalColor + gNoiseValue * (finalColor - noiseOverlay);

	// Step 5: Apply scratches
	if (gRandomValue < gScratchValue)
	{
		// Pick a random spot to show scratches
		float dist = 1.0f / gScratchValue;
		float d = distance(input.TexC, float2(gRandomValue * dist, gRandomValue * dist));
		if (d < 0.4f)
		{
			// Generate the scratch
			float xPeriod = 8.0f;
			float yPeriod = 1.0f;
			float pi = 3.141592f;
			float phase = gTimeLapse;
			float turbulence = snoise(input.TexC * 2.5f);
			float vScratch = 0.5f + (sin(((input.TexC.x * xPeriod + input.TexC.y * yPeriod + turbulence)) * pi + phase) * 0.5f);
			vScratch = clamp((vScratch * 10000.0f) + 0.35f, 0.0f, 1.0f);

			finalColor.xyz *= vScratch;
		}
	}

	// Step 6: Apply vignetting
	// Max distance from center to corner is ~0.7. Scale that to 1.0.
	float d = distance(float2(0.5f, 0.5f), input.TexC) * 1.414213;
	float vignetting = clamp((gOuterVignetting - d) / (gOuterVignetting - gInnerVignetting), 0.0f, 1.0f);
	finalColor.xyz *= vignetting;

	// Apply color
	return float4(finalColor, 1.0f);
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