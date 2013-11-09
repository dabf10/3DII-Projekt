#ifndef _SKINNEDDATA_H_
#define _SKINNEDDATA_H_

#include "Camera.h" //Just wanted XNAmath but includes hate me.
#include <vector>
#include <map>

class SkinnedData
{
private:
	struct AnimationClip;
	struct BoneAnimation;
	struct Keyframe;

public:
	SkinnedData(void);
	~SkinnedData(void);

	void Animate(std::string& clipName, float time, std::vector<XMFLOAT4X4>& transformations);
	void LoadAnimation(std::string* fileName);

private:
	std::vector<int> m_BoneHierarchy;
	std::vector<XMFLOAT4X4> m_BoneOffsets;

	std::map<std::string, AnimationClip> m_AnimationClips;
};

struct SkinnedData::AnimationClip
{
	AnimationClip(void);
	~AnimationClip(void);

	void Interpolate(float time, std::vector <XMFLOAT4X4>& boneTransformations);

	//std::vector<std::vector<XMFLOAT4X4>> Cache;
	std::vector<BoneAnimation> BoneAnimations;
	float ClipLength;
};

struct SkinnedData::BoneAnimation
{
	BoneAnimation(void);
	~BoneAnimation(void);

	void Interpolate(float time, XMFLOAT4X4& matrix) const;

	std::vector<Keyframe> KeyFrames;
};

struct SkinnedData::Keyframe
{
	Keyframe(void);
	~Keyframe(void);

	float Time;
	XMFLOAT3 Position;
	XMFLOAT3 Scale;
	XMFLOAT4 Quaternion;
};

#endif