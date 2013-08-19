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

	// TODO: Kanske kommer behöva spara bool transparent om många saker kan göra
	// genomskinlig, så behöver man bara kolla ett värde senare i model. Ett
	// transparent-värde är bra om man vill se till att transparenta objekt
	// renderas efter opaka.
	struct SurfaceMaterial
	{
		SurfaceMaterial() : MatName(""), Illum(0), Diffuse(0.8f, 0.8f, 0.8f),
			Ambient(0.2f, 0.2f, 0.2f), Specular(1.0f, 1.0f, 1.0f),
			TransmissionFilter(1.0f, 1.0f, 1.0f), DiffuseMapIndex(-1),
			OpticalDensity(1.0f), SpecPower(16.0f), Transparency(1.0f),
			DiffuseTexture("")
		{
		}

		std::string MatName; // Name of material, comes from "newmtl"
		UINT Illum; // Illumination model.
		XMFLOAT3 Diffuse;
		XMFLOAT3 Ambient;
		XMFLOAT3 Specular;
		XMFLOAT3 TransmissionFilter;
		int DiffuseMapIndex; // Index of texture in mMeshSRV to use.
		float OpticalDensity; // How much light bends in material.
		float SpecPower;
		float Transparency;
		std::string DiffuseTexture;
	};

	OBJLoader();

	bool LoadOBJ(
		std::string filename,
		std::vector<UINT>& groupIndexStart,
		std::vector<UINT>& materialToUseForGroup,
		UINT& groupCount,
		bool isRHCoordSys,
		std::vector<SurfaceMaterial>& materials,
		void **vertexData,
		void **indexData,
		long &vertexDataSize,
		UINT &nIndices,
		UINT &indexSize);

private:
	void ReadComment(std::ifstream& fin);
	void ReadVertexDescriptions(std::ifstream& fin, bool isRHCoordSys);
	void ReadGroup(std::ifstream& fin, std::vector<UINT>& groupIndexStart, UINT& groupCount);
	void ReadFace(std::ifstream& fin, bool isRHCoordSys, std::vector<UINT>& groupIndexStart, UINT& groupCount);
	void ReadMtlLib(std::ifstream& fin);
	void ReadUseMtl(std::ifstream& fin);
	void ParseVertexDefinition(std::string vertDef);
	void RetriangulateFace(int firstVIndex, int lastVIndex, int triangleCount, std::istringstream& ss);
    void HandleVertexDuplication();

	void ReadIllum(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);
	void ReadDiffuseColor(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);
	void ReadAmbientColor(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);
	void ReadTransmissionFilter(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);
	void ReadDiffuseMap(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);
	void ReadOpticalDensity(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);
	void ReadNewMtl(std::istringstream& ss, std::vector<SurfaceMaterial>& materials);

private:
	// TODO: Hantera path ordentligt. Just nu är materialets filnamn i förhållande
	// till var obj-filen finns, alltså måste båda filerna finnas i samma mapp.
	std::vector<std::string> mMeshMatLibs; // Holds obj material library filenames.
	std::vector<std::string> mTextureNames; // Don't load same texture twice.

	// Arrays to store model information.
	std::vector<DWORD> mIndices; // Index array of complete vertices.
	std::vector<XMFLOAT3> mVertexPositions; // Unique vertex positions.
	std::vector<XMFLOAT2> mVertexTextureCoordinates; // Unique tex coords.
	std::vector<XMFLOAT3> mVertexNormals; // Unique normals.
	std::vector<Vertex> mCompleteVertices;
	std::vector<std::string> mGroupMaterials; // What material a certain group uses.

	// Vertex definition indices.
	std::vector<UINT> mVertexPositionIndices;
	std::vector<UINT> mVertexTexCoordIndices;
	std::vector<UINT> mVertexNormalIndices;

	// Used to create a default if no tex coords or normals are defined.
	bool mHasTexCoord;
	bool mHasNorm;

	// Temp variables to store into vectors.
	std::string mMeshMaterialsTemp;
	int mVertexPositionIndexTemp;
	int mVertexTexCoordIndexTemp;
	int mVertexNormalIndexTemp;

	char mCheckChar; // To store one char from file at a time.
	int mVIndex; // Vertex index count, i.e. how many vertices index array stores.
	int mTotalVertices;
	int mMeshTriangles;
};

#endif // _OBJLoader_H_