#ifndef _MODEL_H_
#define _MODEL_H_

#include "OBJLoader.h"
#include <D3D11.h>

class Model
{
	typedef OBJLoader::Vertex Vertex;

public:
	Model();
	~Model();

	bool LoadOBJ(const char *filename, bool isRHCoordSys, ID3D11Device *device, std::vector<UINT>& materialToUseForGroup,
		std::vector<OBJLoader::SurfaceMaterial>& materials);
	void Render(ID3D11DeviceContext *context);
	void RenderSubMesh(ID3D11DeviceContext *context, int submesh);

	UINT SubMeshes() const { return mMeshGroups; }

private:
	Model(const Model& rhs );
	Model& operator=(const Model& rhs);

private:
	ID3D11Buffer *mVBuffer;
	ID3D11Buffer *mIBuffer;

	std::vector<UINT> mGroupIndexStart; // Where in the index buffer a group starts to draw.
	UINT mMeshGroups; // Number of groups in mesh.
};

#endif // _MODEL_H_