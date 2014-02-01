#include "Model.h"
#include "DXUT.h"

Model::Model() : mVBuffer(0), mIBuffer(0)
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

void Model::Render( ID3D11DeviceContext *pd3dImmediateContext )
{
	UINT stride = sizeof( Vertex );
	UINT offset = 0;
	pd3dImmediateContext->IASetVertexBuffers( 0, 1, &mVBuffer, &stride, &offset );
	pd3dImmediateContext->IASetIndexBuffer( mIBuffer, DXGI_FORMAT_R32_UINT, 0 );

	pd3dImmediateContext->DrawIndexed(mBatches[mBatches.size()-1], 0, 0);
}

void Model::RenderBatch( ID3D11DeviceContext *pd3dImmediateContext, int batch )
{
	UINT stride = sizeof( Vertex );
	UINT offset = 0;
	pd3dImmediateContext->IASetVertexBuffers( 0, 1, &mVBuffer, &stride, &offset );
	pd3dImmediateContext->IASetIndexBuffer( mIBuffer, DXGI_FORMAT_R32_UINT, 0 );

	int start = mBatches[batch];
	int count = mBatches[batch + 1] - start;
	pd3dImmediateContext->DrawIndexed( count, start, 0 );
}

bool Model::LoadOBJ( const char *filename, bool rightHanded, ID3D11Device *device )
{
	OBJLoader loader;
	if (!loader.LoadOBJ(filename, rightHanded)) return false;

	const void *vertexData = loader.VertexData();
	const void *indexData = loader.IndexData();
	UINT vertexDataSize = loader.VertexDataSize();
	UINT indices = loader.NumIndices();
	UINT indexSize = loader.IndexSize();
	mBatches = loader.Batches( );

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