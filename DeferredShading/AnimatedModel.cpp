#include "AnimatedModel.h"

AnimatedModel::AnimatedModel()
{
	mAnimationTime = 0;
	mAnimation = new SkinnedData();
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
		mAnimationTime -= clipLength; //loop

	mAnimation->Animate(mCurrentClipName, mAnimationTime, mAnimationMatrices);
}

void AnimatedModel::SetCurrentClip(std::string clipName)
{
	mCurrentClipName = clipName;
}

AnimatedModel::Vertex* AnimatedModel::ConvertVertices(std::vector<gnomeImporter::vertex> importedVertices)
{
	Vertex* convertedVertices = new Vertex[importedVertices.size()];

	for(int i = 0; i < importedVertices.size(); ++i)
	{
		convertedVertices[i].Position.x = importedVertices[i].position[0];
		convertedVertices[i].Position.y = importedVertices[i].position[1];
		convertedVertices[i].Position.z = importedVertices[i].position[2];

		convertedVertices[i].Normal.x	= importedVertices[i].normal[0];
		convertedVertices[i].Normal.y	= importedVertices[i].normal[1];
		convertedVertices[i].Normal.z	= importedVertices[i].normal[2];

		convertedVertices[i].TexCoord.x = importedVertices[i].uv[0];
		convertedVertices[i].TexCoord.y = importedVertices[i].uv[1];
	}

	return convertedVertices;
}

