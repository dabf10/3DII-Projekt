#include "Model.h"
#include "DXUT.h"

Model::Model() : mVBuffer(0), mIBuffer(0), mMeshGroups(0)
{
}

// OBS! Vid tilldelning kommer pekarna f�r dessa objekt att kopieras �ver, men
// inte datan de pekar p�, och n�r den tempor�ra Modelvariabeln destrueras
// rensas datan som pekarna pekar p� ut och det blir problem. F�r att l�sa det
// kan man g�ra egna copy constructor, copy assignment operator, och destructor.
// Kom ih�g Rule of Three: Beh�vs en av dessa, beh�vs antagligen alla. I detta
// fall anv�nds en Initialize, men destructorn k�rs vid tilldelning icke desto
// mindre eftersom f�rst: Model mModel; och senare mModel = Model(); Konstruktor
// k�rs tv� g�nger och en tilldelning sker > destruktorn anropas p� tempvariabel.
Model::~Model()
{
	SAFE_RELEASE(mVBuffer);
	SAFE_RELEASE(mIBuffer);
}

void Model::Render(ID3D11DeviceContext *context)
{
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &mVBuffer, &stride, &offset);
	context->IASetIndexBuffer(mIBuffer, DXGI_FORMAT_R32_UINT, 0);

	context->DrawIndexed(mGroupIndexStart[mMeshGroups], 0, 0);
}

void Model::RenderSubMesh(ID3D11DeviceContext *context, int submesh)
{
	int indexStart = mGroupIndexStart[submesh];
	int indexDrawAmount = mGroupIndexStart[submesh+1] - indexStart;

	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	context->IASetVertexBuffers(0, 1, &mVBuffer, &stride, &offset);
	context->IASetIndexBuffer(mIBuffer, DXGI_FORMAT_R32_UINT, 0);

	context->DrawIndexed(indexDrawAmount, indexStart, 0);
}

bool Model::LoadOBJ(const char *filename, bool isRHCoordSys, ID3D11Device *device, std::vector<UINT>& materialToUseForGroup,
	std::vector<OBJLoader::SurfaceMaterial>& materials)
{
	void *vertexData = nullptr;
	void *indexData = nullptr;
	long vertexDataSize = 0;
	UINT indices = 0;
	UINT indexSize = 0;

	OBJLoader loader;
	if (!loader.LoadOBJ(filename, mGroupIndexStart, materialToUseForGroup, mMeshGroups,
		isRHCoordSys, materials, &vertexData, &indexData, vertexDataSize, indices, indexSize)) return false;

	// Create index buffer
	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = indexSize * indices;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	ibd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = indexData;
	HRESULT hr;
	V(device->CreateBuffer(&ibd, &iinitData, &mIBuffer));

	// Create vertex buffer
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.ByteWidth = vertexDataSize;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = vertexData;
	vinitData.SysMemPitch = 0;
	vinitData.SysMemSlicePitch = 0;
	V(device->CreateBuffer(&vbd, &vinitData, &mVBuffer));

	return true;
}