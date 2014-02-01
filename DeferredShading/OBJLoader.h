#ifndef _OBJLoader_H_
#define _OBJLoader_H_

#include <string>
#include <vector>
#include <Windows.h>
#include <xnamath.h>

class OBJLoader
{
public:
	struct Vertex
	{
		XMFLOAT3 Position;
		XMFLOAT2 TexCoord;
		XMFLOAT3 Normal;
	};

	OBJLoader();
	~OBJLoader();

	bool LoadOBJ( const char *filename, bool rightHanded = true );

	const void *VertexData( ) const { return mCompleteVertices.data(); }
	UINT VertexDataSize( ) const { return sizeof(Vertex) * mCompleteVertices.size(); }
	const void *IndexData( ) const { return mCompleteIndices.data(); }
	UINT IndexSize( ) const { return sizeof(UINT); }
	UINT NumIndices( ) const { return mCompleteIndices.size(); }

	const std::vector<UINT> &Batches( ) const { return mBatches; }

private:
	struct Material
	{
		char *name;
		std::vector<UINT> indices;
	};

private:
	void ReadVertexDescriptions(std::ifstream& fin, bool rightHanded);
	void ReadFace(std::ifstream& fin);
	void ReadUsemtl(std::ifstream& fin);
	void ReadComment(std::ifstream& fin);

	void ParseVertexDefinition(std::string vertDef, UINT &position, UINT &texCoord, UINT &normal);
	void RetriangulateFace(int firstVIndex, int triangleCount, std::istringstream& ss);

	int GetMaterial(const char *name);

private:
	std::vector<Vertex> mCompleteVertices;
	std::vector<UINT> mCompleteIndices;
	std::vector<UINT> mBatches;

	std::vector<XMFLOAT3> mPositions;
	std::vector<XMFLOAT2> mTexCoords;
	std::vector<XMFLOAT3> mNormals;
	std::vector<UINT> mPositionIndices;
	std::vector<UINT> mTexCoordIndices;
	std::vector<UINT> mNormalIndices;

	// To support mtl parsing: When newmtl is read, the material from this array could be found using
	// GetMaterial(). Then one could simply fill that material with data. If not found, create a new one.
	// When parsing obj, materials would then be empty and could be filled with default data.
	// This is if one would actually use the material from the .mtl file.
	std::vector<Material> mMaterials;
	int mCurrentMaterial;

	UINT mTotalVertices;
};

#endif // _OBJLoader_H_