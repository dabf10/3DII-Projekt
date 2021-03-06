#include "AnimatedModel.h"

AnimatedModel::AnimatedModel()
{
	mAnimationTime = 0;
	mAnimation = new SkinnedData();
	mLoop = true;
}

AnimatedModel::~AnimatedModel()
{
	SAFE_RELEASE(mVertexBuffer);
	delete mAnimation; mAnimation = nullptr;
}

bool AnimatedModel::LoadGnome(const char* filename, ID3D11Device* device)
{
	gnomeImporter importer;
	std::vector<gnomeImporter::vertex> vertexList;
	std::vector<gnomeImporter::material> materialList;
	std::vector<int> materialSwitchIndices;

	if(!importer.getVectors(filename, materialList, vertexList, materialSwitchIndices))
		return false;	//This is never going to happen since the importer never returns false >.>
	
	mVertexCount	= vertexList.size();

	Vertex* convertedVertices = ConvertVertices(vertexList);
	const void*		vertexData		= convertedVertices;
	unsigned int	vertexDataSize	= sizeof(Vertex) * mVertexCount;

	D3D11_BUFFER_DESC vertexBufferDescription;
	vertexBufferDescription.Usage				= D3D11_USAGE_IMMUTABLE;
	vertexBufferDescription.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDescription.CPUAccessFlags		= 0;
	vertexBufferDescription.MiscFlags			= 0;
	vertexBufferDescription.StructureByteStride = 0;
	vertexBufferDescription.ByteWidth = vertexDataSize;

	D3D11_SUBRESOURCE_DATA vertexInitData;
	vertexInitData.pSysMem			= vertexData;
	vertexInitData.SysMemPitch		= 0;
	vertexInitData.SysMemSlicePitch = 0;

	if (FAILED(device->CreateBuffer(&vertexBufferDescription, &vertexInitData, &mVertexBuffer)))
	{
		delete[] convertedVertices;
		return false;
	}
	delete[] convertedVertices;

	mAnimation->LoadAnimation(filename);

	return true;
}

void AnimatedModel::Render(ID3D11DeviceContext* context)
{
	unsigned int stride = sizeof(Vertex);
	unsigned int offset = 0;

	context->IASetVertexBuffers(0, 1, &mVertexBuffer, &stride, &offset);

	context->Draw(mVertexCount, 0);
}

void AnimatedModel::Animate(float dt)
{
	//Reset matrices
	mAnimationMatrices = std::vector<XMFLOAT4X4>();

	mAnimationTime += dt;
	float clipLength = mAnimation->GetClipLength(mCurrentClipName);
	if(mAnimationTime > clipLength)
	{
		if(mLoop)
		{
			mAnimationTime -= clipLength;	
		}
		else
		{
			mAnimationTime = clipLength;
		}
	}
	

	mAnimation->Animate(mCurrentClipName, mAnimationTime, mAnimationMatrices);
}

void AnimatedModel::SetCurrentClip(std::string clipName)
{
	mCurrentClipName = clipName;
}

std::vector<XMFLOAT4X4> AnimatedModel::GetAnimiationMatrices()
{
	return mAnimationMatrices;
}

void AnimatedModel::SetLoop(bool value)
{
	mLoop = value;
}

bool AnimatedModel::GetLoop()
{
	return mLoop;
}

AnimatedModel::Vertex* AnimatedModel::ConvertVertices(std::vector<gnomeImporter::vertex> importedVertices)
{
	Vertex* convertedVertices = new Vertex[importedVertices.size()];

	for(int i = 0; i < importedVertices.size(); i += 3)
	{
		convertedVertices[i].Position.x = importedVertices[i+2].position[0];
		convertedVertices[i].Position.y = importedVertices[i+2].position[1];
		convertedVertices[i].Position.z = -importedVertices[i+2].position[2];

		convertedVertices[i].Normal.x = importedVertices[i+2].normal[0];
		convertedVertices[i].Normal.y = importedVertices[i+2].normal[1];
		convertedVertices[i].Normal.z = importedVertices[i+2].normal[2];

		convertedVertices[i].TexCoord.x = importedVertices[i+2].uv[0];
		convertedVertices[i].TexCoord.y = importedVertices[i+2].uv[1];

		convertedVertices[i].Weights.x = importedVertices[i+2].skinWeight[0];
		convertedVertices[i].Weights.y = importedVertices[i+2].skinWeight[1];
		convertedVertices[i].Weights.z = importedVertices[i+2].skinWeight[2];

		convertedVertices[i].Bones[0] = importedVertices[i+2].jointIndex[0];
		convertedVertices[i].Bones[1] = importedVertices[i+2].jointIndex[1];
		convertedVertices[i].Bones[2] = importedVertices[i+2].jointIndex[2];
		convertedVertices[i].Bones[3] = importedVertices[i+2].jointIndex[3];

		// -----------------------------

		convertedVertices[i+1].Position.x = importedVertices[i+1].position[0];
		convertedVertices[i+1].Position.y = importedVertices[i+1].position[1];
		convertedVertices[i+1].Position.z = -importedVertices[i+1].position[2];

		convertedVertices[i+1].Normal.x = importedVertices[i+1].normal[0];
		convertedVertices[i+1].Normal.y = importedVertices[i+1].normal[1];
		convertedVertices[i+1].Normal.z = importedVertices[i+1].normal[2];

		convertedVertices[i+1].TexCoord.x = importedVertices[i+1].uv[0];
		convertedVertices[i+1].TexCoord.y = importedVertices[i+1].uv[1];

		convertedVertices[i+1].Weights.x = importedVertices[i+1].skinWeight[0];
		convertedVertices[i+1].Weights.y = importedVertices[i+1].skinWeight[1];
		convertedVertices[i+1].Weights.z = importedVertices[i+1].skinWeight[2];

		convertedVertices[i+1].Bones[0] = importedVertices[i+1].jointIndex[0];
		convertedVertices[i+1].Bones[1] = importedVertices[i+1].jointIndex[1];
		convertedVertices[i+1].Bones[2] = importedVertices[i+1].jointIndex[2];
		convertedVertices[i+1].Bones[3] = importedVertices[i+1].jointIndex[3];

		// ----------------------------

		convertedVertices[i+2].Position.x = importedVertices[i].position[0];
		convertedVertices[i+2].Position.y = importedVertices[i].position[1];
		convertedVertices[i+2].Position.z = -importedVertices[i].position[2];

		convertedVertices[i+2].Normal.x = importedVertices[i].normal[0];
		convertedVertices[i+2].Normal.y = importedVertices[i].normal[1];
		convertedVertices[i+2].Normal.z = importedVertices[i].normal[2];

		convertedVertices[i+2].TexCoord.x = importedVertices[i].uv[0];
		convertedVertices[i+2].TexCoord.y = importedVertices[i].uv[1];

		convertedVertices[i+2].Weights.x = importedVertices[i].skinWeight[0];
		convertedVertices[i+2].Weights.y = importedVertices[i].skinWeight[1];
		convertedVertices[i+2].Weights.z = importedVertices[i].skinWeight[2];

		convertedVertices[i+2].Bones[0] = importedVertices[i].jointIndex[0];
		convertedVertices[i+2].Bones[1] = importedVertices[i].jointIndex[1];
		convertedVertices[i+2].Bones[2] = importedVertices[i].jointIndex[2];
		convertedVertices[i+2].Bones[3] = importedVertices[i].jointIndex[3];
	}

	return convertedVertices;
}