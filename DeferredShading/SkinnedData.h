#ifndef _SKINNEDDATA_H_
#define _SKINNEDDATA_H_

#include "Camera.h" //Just wanted XNAmath but includes hate me.
#include <vector>
#include <map>
#include <string>
#include <fstream>

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
	void LoadAnimation(std::string fileName);
	void ParseAnimation(std::string fileName);
	void WriteSerializedAnimation(std::string fileName);
	void ReadBinaryAnimation(std::string fileName, size_t length);
	float GetClipLength(std::string &clipName );


private:
	std::vector<int> m_BoneHierarchy;
	std::vector<XMFLOAT4X4> m_BoneOffsets;

	std::map<std::string, AnimationClip> m_AnimationClips;

private:
	//Constants for serialization
	static const size_t bufferSize	= 8388608;
	static const size_t uintSize	= sizeof(unsigned int);
	static const size_t floatSize	= sizeof(float);
	static const size_t intSize		= sizeof(int);
	static const size_t	float4x4Size = sizeof(XMFLOAT4X4);
};

struct SkinnedData::AnimationClip
{
	AnimationClip(void);
	~AnimationClip(void);

	void Interpolate(float time, std::vector <XMFLOAT4X4>& boneTransformations);
	void Serialize(char* &pos, size_t& readCount);

	std::vector<std::vector<XMFLOAT4X4>> Cache;
	std::vector<BoneAnimation> BoneAnimations;
	float ClipLength;
};

struct SkinnedData::BoneAnimation
{
	BoneAnimation(void);
	~BoneAnimation(void);

	void Interpolate(float time, XMFLOAT4X4& matrix) const;
	void Serialize(char* &pos, size_t& readCount);

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