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

	bool LoadOBJ( const char *filename, bool rightHanded, ID3D11Device *device );
	void Render( ID3D11DeviceContext *pd3dImmediateContext );
	void RenderBatch( ID3D11DeviceContext *pd3dImmediateContext, int batch );

	UINT Batches( ) const { return mBatches.size() - 1; }

private:
	Model( const Model& rhs );
	Model& operator=( const Model& rhs );

private:
	ID3D11Buffer *mVBuffer;
	ID3D11Buffer *mIBuffer;

	std::vector<UINT> mBatches; // Where in the index buffer a batch starts to draw.
};

#endif // _MODEL_H_