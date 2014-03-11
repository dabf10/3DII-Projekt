#ifndef ANIMATED_MODEL_H_
#define ANIMATED_NODEL_H_

#include <D3D11.h>
#include "DXUT.h"
#include <xnamath.h>
#include "GnomeImporter.h"


class AnimatedModel
{
	struct Vertex
	{
		XMFLOAT3 Position;
		XMFLOAT2 TexCoord;
		XMFLOAT3 Normal;
	};

public:
	AnimatedModel();
	~AnimatedModel();

	bool LoadGnome(const char* fileName, ID3D11Device* device);
	void Render(ID3D11DeviceContext* context);

private:
	Vertex* ConvertVertices(std::vector<gnomeImporter::vertex> importedVertices);

	ID3D11Buffer* mVertexBuffer;
	unsigned int mVertexCount;
};
#endif