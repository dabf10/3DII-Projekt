#include "SkinnedData.h"

#pragma region SkinnedData
SkinnedData::SkinnedData(void)
{}

SkinnedData::~SkinnedData(void)
{}

void SkinnedData::Animate(std::string& clipName, float time, std::vector<XMFLOAT4X4>& transformations)
{
	std::map<std::string, std::map<std::string, SkinnedData::AnimationClip>>::iterator clip = m_AnimationClips.find(clipName); //This gets the whole keyValPair but that will do.

	//	// Get cache index
	//int cacheIndex = (int)( time / CACHE_TIME_STEP );

	//// Detect overflow in index
	//if( clip->second.AnimationCache.size( ) <= (unsigned int)cacheIndex )
	//	cacheIndex = clip->second.AnimationCache.size( ) - 1;

	//	// Select cache or set time to create cache from
	//if( clip->second.AnimationCache[cacheIndex].size( ) == 0 )
	//	time = cacheIndex * CACHE_TIME_STEP;
	//else
	//{
	//	transforms = clip->second.AnimationCache[ cacheIndex ];
	//	return;
	//}

	//Initialize
	unsigned int boneCount = m_BoneOffsets.size();

		/*if( transforms.size( ) < numBones )
		transforms.resize( numBones );*/

	std::vector<XMFLOAT4X4> toParentTransformations = std::vector<XMFLOAT4X4>(boneCount);
	std::vector<XMFLOAT4X4> toRootTransformations = std::vector<XMFLOAT4X4>(boneCount);

	//Generate toParentTransformations
	clip->second.Interpolate(time, toParentTransformations);

	//Use them to generate toroot transformations
	toRootTransformations[0] = toParentTransformations[0];
	for(unsigned int i = 0; i < boneCount; ++i)
	{
		XMMATRIX toParent = XMLoadFloat4x4(&toParentTransformations[i]);

		int parentIndex = m_BoneHierarchy[i];
		XMMATRIX parentToRoot = XMLoadFloat4x4(&toRootTransformations[parentIndex]);

		XMMATRIX toRoot = XMMatrixMultiply(toParent, parentToRoot);
	}

	//return
	for(unsigned int i = 0; i < boneCount; ++i)
	{
		XMMATRIX offset = XMLoadFloat4x4(&m_BoneOffsets[i]);
		XMMATRIX toRoot = XMLoadFloat4x4(&toRootTransformations[i]);
		XMStoreFloat4x4(& transformations[i], XMMatrixMultiply(offset, toRoot));
	}

	//clip->second.AnimationCache[ cacheIndex ] = std::vector<XMFLOAT4X4>( transforms );
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
#pragma endregion

#pragma region BoneAnimation
SkinnedData::BoneAnimation::BoneAnimation(void)
{}

SkinnedData::BoneAnimation::~BoneAnimation(void)
{}

void SkinnedData::BoneAnimation::Interpolate(float time, XMFLOAT4X4& matrix) const
{
	XMVECTOR scale;
	XMVECTOR position;
	XMVECTOR quaternion;
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
		for(int i = 0; i < KeyFrames.size(); ++i)
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
#pragma endregion

#pragma region Keyframe
SkinnedData::Keyframe::Keyframe(void)
{
	ZeroMemory(this, sizeof(Keyframe));
}

SkinnedData::Keyframe::~Keyframe(void)
{}
#pragma endregion