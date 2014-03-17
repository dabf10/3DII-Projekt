#include "SkinnedData.h"

#pragma region SkinnedData
SkinnedData::SkinnedData(void)
{
	m_BoneHierarchy = std::vector<int>();
	m_BoneOffsets = std::vector<XMFLOAT4X4>();
	m_AnimationClips = std::map<std::string, AnimationClip>();
}

SkinnedData::~SkinnedData(void)
{}

void SkinnedData::Animate(std::string& clipName, float time, std::vector<XMFLOAT4X4>& transformations)
{
	//std::map<std::string, SkinnedData::AnimationClip>::iterator clip = m_AnimationClips.find(clipName); //This gets the whole keyValPair but that will do.
	auto clip = m_AnimationClips.find(clipName); //This gets the whole keyValPair but that will do.

	//	// Get cache index
	int cacheIndex = (int)( time / 0.01f );

	// Detect overflow in index
	if( clip->second.Cache.size( ) <= (unsigned int)cacheIndex )
		cacheIndex = clip->second.Cache.size( ) - 1;

		// Select cache or set time to create cache from
	if( clip->second.Cache[cacheIndex].size( ) == 0 )
		time = cacheIndex * 0.01f;
	else
	{
		transformations = clip->second.Cache[ cacheIndex ];
		return;
	}

	//Initialize
	unsigned int boneCount = m_BoneOffsets.size();

	if( transformations.size() < boneCount)
		transformations.resize(boneCount);

	std::vector<XMFLOAT4X4> toParentTransformations = std::vector<XMFLOAT4X4>(boneCount);
	std::vector<XMFLOAT4X4> toRootTransformations = std::vector<XMFLOAT4X4>(boneCount);

	//Generate toParentTransformations
	clip->second.Interpolate(time, toParentTransformations);

	//Use them to generate toroot transformations
	toRootTransformations[0] = toParentTransformations[0];
	for(unsigned int i = 1; i < boneCount; ++i)
	{
		XMMATRIX toParent = XMLoadFloat4x4(&toParentTransformations[i]);

		int parentIndex = m_BoneHierarchy[i];
		XMMATRIX parentToRoot = XMLoadFloat4x4(&toRootTransformations[parentIndex]);

		XMMATRIX toRoot = XMMatrixMultiply(toParent, parentToRoot);

		XMStoreFloat4x4( &toRootTransformations[i], toRoot );
	}

	//return
	for(unsigned int i = 0; i < boneCount; ++i)
	{
		XMMATRIX offset = XMLoadFloat4x4(&m_BoneOffsets[i]);
		XMMATRIX toRoot = XMLoadFloat4x4(&toRootTransformations[i]);
		XMStoreFloat4x4(& transformations[i], XMMatrixMultiply(offset, toRoot));
	}

	clip->second.Cache[ cacheIndex ] = std::vector<XMFLOAT4X4>( transformations );
}

void SkinnedData::LoadAnimation(std::string fileName)
{
	//std::string binaryPath = fileName + "BINARYANIMATION";
	//std::ifstream binaryFile(binaryPath, std::ifstream::binary);
	//if(binaryFile.is_open())
	//{
	//	//Read the size here since we use a different method for opening the file in "ReadBinaryAnimation".
	//	binaryFile.seekg (0,binaryFile.end);
	//	size_t length = (size_t)binaryFile.tellg();
	//	binaryFile.seekg (0, binaryFile.beg);
	//	ReadBinaryAnimation(binaryPath, length);
	//}
	//else
	//{
	//	binaryFile.close();
		ParseAnimation(fileName);
//	}
}

void SkinnedData::ParseAnimation(std::string fileName)
{
	std::ifstream file;
	file.open( fileName , std::ios::in );

	std::string name;
	int boneID = -1;

	char buffer[512] = {0};
	while(file.good())
	{
		file.getline(buffer, sizeof(buffer ));

		int   i[2]  = {0};
		float f[16] = {0};
		char  c[64] = {0};

		// Fetch the 4x4 offset matric for the bones
		int boneOffset = sscanf_s( buffer, "jointOffset%d %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
									&i[0],
									&f[0],  &f[1],  &f[2],  &f[3], 
									&f[4],  &f[5],  &f[6],  &f[7], 
									&f[8],  &f[9],  &f[10], &f[11], 
									&f[12], &f[13], &f[14], &f[15] );

		// Fetch the parent for each bone
		int boneParent = sscanf_s( buffer, "jointParent%d %d", &i[0], &i[1] );

		// Get the name of the animation clip
		int animationClip = sscanf_s( buffer, "AnimationClip %s", c, sizeof( c ) );

		// Get the ID of the current bone to animate
		int animationBone = sscanf_s( buffer, "\tjoint%d", &boneID );

		// Get a keyframe for the current bone
		int keyframe = sscanf_s( buffer, "\t\tTime: %f Pos: %f %f %f Scale: %f %f %f Quat: %f %f %f %f",
											&f[0],
											&f[1], &f[2], &f[3],
											&f[4], &f[5], &f[6],
											&f[7], &f[8], &f[9], &f[10] );

		if( boneOffset == 17 ) // Store the offset matrix
			m_BoneOffsets.push_back( XMFLOAT4X4( f ) );
		if( boneParent == 2 ) // Store the parent
			m_BoneHierarchy.push_back( i[1] );
		if( animationClip == 1 )
		{
			// Create a new animation clip
			name = std::string( c );
			m_AnimationClips[name] = AnimationClip( );
		}
		if( animationBone == 1 ) // Add a bone animation
			m_AnimationClips[name].BoneAnimations.push_back( BoneAnimation( ) );
		if( keyframe == 11 )
		{
			// Add a new keyframe
			Keyframe kf;

			kf.Time = f[0];

			kf.Position		= XMFLOAT3( f[1], f[2], f[3] );
			kf.Scale		= XMFLOAT3( f[4], f[5], f[6] );
			kf.Quaternion	= XMFLOAT4( f[7], f[8], f[9], f[10] );

			if( kf.Time > m_AnimationClips[name].ClipLength )
				m_AnimationClips[name].ClipLength = kf.Time;

			m_AnimationClips[name].BoneAnimations[boneID].KeyFrames.push_back( kf );
		}
	}

	// Allocate memory for the cache (resize the vector)
	for( std::map<std::string, AnimationClip>::iterator it = m_AnimationClips.begin( ); it != m_AnimationClips.end( ); it++ )
		 it->second.Cache.resize( (unsigned int)(it->second.ClipLength / 0.01 + 1.0f) );

	WriteSerializedAnimation(fileName);
	file.close( );
}

void SkinnedData::WriteSerializedAnimation(std::string fileName)
{
	fileName = fileName + "BINARYANIMATION";
	char* binary	= new char[bufferSize]; //Pointer to the start of the char aray
	char* pos		= binary;				//Pointer to the next position that should be ritten to
	size_t readCount = 0;					//How many bytes have been read in total

	//Save the number of boneHiearchies.
	unsigned int boneHierarchyCount = m_BoneHierarchy.size();
	memcpy_s(pos, bufferSize - readCount, &boneHierarchyCount, uintSize);
	pos += uintSize;
	readCount += uintSize;

	//Save the bonehierarchies
	for(int i = 0; i < boneHierarchyCount; ++i)
	{
		memcpy_s(pos, bufferSize - readCount, &m_BoneHierarchy[i], intSize);
		pos += intSize;
		readCount += intSize;
	}

	//Save the number of boneoffsets
	unsigned int boneOffsetCount = m_BoneOffsets.size();
	memcpy_s(pos, bufferSize - readCount, &boneOffsetCount, uintSize);
	pos += uintSize;
	readCount += uintSize;

	//Save the boneOffsets
	for(int i = 0; i < boneOffsetCount; ++i)
	{
		memcpy_s(pos, bufferSize - readCount, &m_BoneOffsets[i], float4x4Size);
		pos += float4x4Size;
		readCount += float4x4Size;
	}

	//Save the number of elements that are in the map.
	unsigned int mapElementCount = 0;
	mapElementCount = m_AnimationClips.size();
	memcpy_s(pos, bufferSize - readCount, &mapElementCount, uintSize);
	pos += uintSize;
	readCount += uintSize;

	//Save the actual elements in the map
	for (auto it = m_AnimationClips.begin(); it != m_AnimationClips.end(); ++it)
	{
		//Save the length of the string
		unsigned int stringLength = strlen(it->first.c_str()) + 1;
		memcpy_s(pos, bufferSize - readCount, &stringLength, uintSize);
		pos += uintSize;
		readCount += uintSize;

		//Save the actual c-string
		memcpy_s(pos, bufferSize - readCount, it->first.c_str(), stringLength); //+1 is for null terminator
		pos += stringLength;
		readCount += stringLength;


		//Serialize the Animationsclips and its content
		it->second.Serialize(pos, readCount);
	}

	//flush
	std::ofstream ofs = std::ofstream(fileName, std::ofstream::binary);
	ofs.write(binary, readCount); //Ingen nullterminator
	ofs.close();

	//Cleanup
	delete[] binary;
}

void SkinnedData::ReadBinaryAnimation(std::string fileName, size_t fileLength)
{
	//Read the file
	HANDLE fileHandle = CreateFileA(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	char* buffer = new char[fileLength];
	char* pos = buffer;
	DWORD bytesread = 0;
	ReadFile(fileHandle, buffer, fileLength, &bytesread, NULL);

	//Copy data

	//bonehierarchy count
	unsigned int boneHierarchyCount = 0;
	memcpy_s(&boneHierarchyCount, uintSize, pos, uintSize);
	pos += uintSize;
	bytesread = uintSize;

	//Bonehierarchies
	int* tempBoneHierarchy = new int[boneHierarchyCount];
	memcpy_s(tempBoneHierarchy, boneHierarchyCount * intSize, pos, boneHierarchyCount * intSize);
	pos += boneHierarchyCount * intSize;
	bytesread += boneHierarchyCount * intSize;
	m_BoneHierarchy.insert(m_BoneHierarchy.end(), &tempBoneHierarchy[0], &tempBoneHierarchy[boneHierarchyCount]);

	//Boneoffset count
	unsigned int boneOffsetCount = 0;
	memcpy_s(&boneOffsetCount, uintSize, pos, uintSize);
	pos += uintSize;
	bytesread += uintSize;

	//BoneOffsets
	XMFLOAT4X4* tempBoneOffsets = new XMFLOAT4X4[boneOffsetCount];
	memcpy_s(tempBoneOffsets, float4x4Size * boneOffsetCount, pos, float4x4Size * boneOffsetCount);
	pos += float4x4Size * boneOffsetCount;
	bytesread += float4x4Size * boneOffsetCount;
	m_BoneOffsets.insert(m_BoneOffsets.end(), &tempBoneOffsets[0], &tempBoneOffsets[boneOffsetCount]);

	//mapSize
	unsigned int mapSize = 0;
	memcpy_s(&mapSize, uintSize, pos, uintSize);
	pos += uintSize;
	bytesread += uintSize;	// = on purpose to reset.

	std::string* clipNames = new std::string[mapSize];
	AnimationClip* clips = new AnimationClip[mapSize];
	unsigned int stringLength = 0;
	for(unsigned int i = 0; i < mapSize; ++i)
	{
		//Read the length of the clip name string
		memcpy_s(&stringLength, uintSize, pos, uintSize);
		pos += uintSize;
		bytesread += uintSize;

		//Read the clip name
		char* readString = new char[stringLength];
		memcpy_s(readString, stringLength, pos, stringLength);
		pos += stringLength;
		bytesread += stringLength;
		clipNames[i] = readString;

		//Read animationclip
		AnimationClip clip = AnimationClip();

		//Read clipLength
		float clipLength = 0.0f;
		memcpy_s(&clipLength, floatSize, pos, floatSize);
		pos += floatSize;
		bytesread += floatSize;
		clip.ClipLength = clipLength;

		//Read BoneAnimationCount
		unsigned int boneAnimationCount = 0;
		memcpy_s(&boneAnimationCount, uintSize, pos, uintSize);
		pos += uintSize;
		bytesread += uintSize;

		//Read BoneAnimations
		BoneAnimation* tempBoneAnimations = new BoneAnimation[boneAnimationCount];
		for (unsigned int j = 0; j < boneAnimationCount; ++j)
		{
			//Read the keyFrameCount
			unsigned int keyFrameCount = 0;
			memcpy_s(&keyFrameCount, uintSize, pos, uintSize);
			pos += uintSize;
			bytesread += uintSize;

			//Read the Keyframes
			Keyframe* tempKeyFrames = new Keyframe[keyFrameCount];
			for (unsigned int k = 0; k < keyFrameCount; ++k)
			{
				memcpy_s(&tempKeyFrames[k], sizeof(Keyframe), pos, sizeof(Keyframe));
				pos += sizeof(Keyframe);
				bytesread += sizeof(Keyframe);
			}
			//Put the keyframes in the bone animations
			tempBoneAnimations[j].KeyFrames.insert(tempBoneAnimations[j].KeyFrames.end(), &tempKeyFrames[0], &tempKeyFrames[keyFrameCount]); //Tar ej med sista elementet så [keyFrameCount} är OK!
			delete[] tempKeyFrames;
		}
		//Put the bone animations in the animationclip
		clip.BoneAnimations.insert(clip.BoneAnimations.end(), &tempBoneAnimations[0], &tempBoneAnimations[boneAnimationCount]);
		clips[i] = clip;

		delete[] readString;
		delete[] tempBoneAnimations;
	}

	//Put the read Animationsclips and their respective names in the map
	for(unsigned int i = 0; i < mapSize; ++i)
	{
		m_AnimationClips[clipNames[i]] = clips[i];
	}

	//Cleanup
		delete[] clipNames;
		delete[] clips;
		delete[] tempBoneOffsets;
		delete[] tempBoneHierarchy;
		delete[] buffer;
}

float SkinnedData::GetClipLength(std::string& clipName)
{
	auto clip = m_AnimationClips.find(clipName);
	return clip->second.ClipLength;
}



#pragma endregion

#pragma region AnimationClip
SkinnedData::AnimationClip::AnimationClip(void)
{
	ClipLength = 0.0f;
}

SkinnedData::AnimationClip::~AnimationClip(void)
{}

void SkinnedData::AnimationClip::Interpolate(float time, std::vector<XMFLOAT4X4>& boneTransformations)
{
	//Interpolate all bones in the animation
	unsigned int i = 0;
	std::vector<BoneAnimation>::const_iterator iterator = BoneAnimations.begin();
	for( ; iterator != BoneAnimations.end(); ++iterator, ++i)
	{
		iterator->Interpolate(time, boneTransformations[i]);
	}
}

void SkinnedData::AnimationClip::Serialize(char* &pos, size_t& readCount)
{
	//Save ClipLength
	memcpy_s(pos, bufferSize - readCount, &ClipLength, floatSize);
	pos += floatSize;
	readCount += floatSize;

	//Save the number of elements in boneAnimations
	unsigned int elementCount = BoneAnimations.size();
	memcpy_s(pos, bufferSize - readCount, &elementCount, uintSize);
	pos += uintSize;
	readCount += uintSize;

	//Save boneAnimations
	for (unsigned int i = 0; i < elementCount; ++i)
	{
		BoneAnimations[i].Serialize(pos, readCount);
	}
}
#pragma endregion

#pragma region BoneAnimation
SkinnedData::BoneAnimation::BoneAnimation(void)
{}

SkinnedData::BoneAnimation::~BoneAnimation(void)
{}

void SkinnedData::BoneAnimation::Interpolate(float time, XMFLOAT4X4& matrix) const
{
	XMVECTOR scale = XMVECTOR();
	XMVECTOR position = XMVECTOR();
	XMVECTOR quaternion = XMVECTOR();
	XMVECTOR zero = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);

	if(time <= KeyFrames.front().Time) //If this is the start -> Use first keyframe
	{
		scale = XMLoadFloat3(&KeyFrames.front().Scale); 
		position = XMLoadFloat3(&KeyFrames.front().Position);
		quaternion = XMLoadFloat4(&KeyFrames.front().Quaternion);
	}
	else if(time >= KeyFrames.back().Time) //If this is the end -> Use the last keyframe
	{
		scale = XMLoadFloat3(&KeyFrames.back().Scale); 
		position = XMLoadFloat3(&KeyFrames.back().Position);
		quaternion = XMLoadFloat4(&KeyFrames.back().Quaternion);
	}
	else //Interpolate
	{
		for(unsigned int i = 0; i < KeyFrames.size(); ++i)
		{
			if(time >= KeyFrames[i].Time && time <= KeyFrames[i+1].Time)
			{
				//Calculate interpolation percentage
				float interpolationPercentage = (time - KeyFrames[i].Time) / (KeyFrames[i+1].Time - KeyFrames[i].Time);

				//Interpolate Scale
				XMVECTOR scale0 = XMLoadFloat3(&KeyFrames[i].Scale);
				XMVECTOR scale1 = XMLoadFloat3(&KeyFrames[i+1].Scale);
				scale = XMVectorLerp(scale0, scale1, interpolationPercentage);

				//Interpolate Position
				XMVECTOR position0 = XMLoadFloat3(&KeyFrames[i].Scale);
				XMVECTOR position1 = XMLoadFloat3(&KeyFrames[i+1].Scale);
				position = XMVectorLerp(position0, position1, interpolationPercentage);

				//Interpolate Quaternion (slerp)
				XMVECTOR quaternion0 = XMLoadFloat4(&KeyFrames[i].Quaternion);
				XMVECTOR quaternion1 = XMLoadFloat4(&KeyFrames[i+1].Quaternion);
				quaternion = XMQuaternionSlerp(quaternion0, quaternion1, interpolationPercentage);

				break;
			}
		}
	}

	//return
	XMStoreFloat4x4(&matrix, XMMatrixAffineTransformation( scale, zero, quaternion, position));
}

void SkinnedData::BoneAnimation::Serialize(char* &pos, size_t& readCount)
{
	unsigned int elementCount = KeyFrames.size();
	memcpy_s(pos, bufferSize - readCount, &elementCount, uintSize);
	pos += uintSize;
	readCount += uintSize;

	for (unsigned int i = 0; i < elementCount; ++i)
	{
		memcpy_s(pos, bufferSize - readCount, &KeyFrames[i], sizeof(Keyframe));
		pos += sizeof(Keyframe);
		readCount += sizeof(Keyframe);
	}
}
#pragma endregion

#pragma region Keyframe
SkinnedData::Keyframe::Keyframe(void)
{
	ZeroMemory(this, sizeof(Keyframe));
}

SkinnedData::Keyframe::~Keyframe(void)
{}
#pragma endregion