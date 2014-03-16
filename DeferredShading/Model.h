#ifndef _MODEL_H_
#define _MODEL_H_

#include "OBJLoader.h"
#include "GnomeImporter.h"
#include <D3D11.h>

class Model
{
	typedef OBJLoader::Vertex Vertex;

public:
	Model();
	~Model();

	bool LoadOBJ( const char *filename, bool rightHanded, ID3D11Device *device );
	bool LoadGnome(const char* filename, ID3D11Device* device);
	void Render( ID3D11DeviceContext *pd3dImmediateContext );
	void RenderBatch( ID3D11DeviceContext *pd3dImmediateContext, int batch );
	bool SerializeToFile(std::string fileName, const void* vertices, unsigned int vertexTotalSize, const void* indices, unsigned int indexTotalSize);
	bool LoadBinary(std::string fileName, ID3D11Device* device);

	UINT Batches( ) const { return mBatches.size() - 1; }

private:
	Model( const Model& rhs );
	Model& operator=( const Model& rhs );

	Vertex* ConvertVertices(std::vector<gnomeImporter::vertex> importedVertices);
	void WriteBinary(char*& position, unsigned int bufferSize, unsigned int& bytesWritten, const void* data, unsigned int maxCount);
	void ReadBinary(char*& position, void* data, unsigned int dataSize, unsigned int& bytesRead);
	bool BinaryExists(std::string fileName);

private:
	ID3D11Buffer *mVBuffer;
	ID3D11Buffer *mIBuffer;

	std::vector<UINT> mBatches; // Where in the index buffer a batch starts to draw.
};

#endif // _MODEL_H_