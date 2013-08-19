#include "OBJLoader.h"
#include <fstream>
#include <sstream>

using namespace std;

OBJLoader::OBJLoader() : mHasTexCoord(false),
	mHasNorm(false), mVIndex(0), mTotalVertices(0), mMeshTriangles(0)
{
}

bool OBJLoader::LoadOBJ(
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
	UINT &indexSize)
{
	// Open file for read.
	ifstream fin(filename.c_str());

	if (!fin)
		return false;

	while(fin)
	{
		mCheckChar = fin.get(); // Get next char.

		switch (mCheckChar)
		{
		case '#':			ReadComment(fin);							break;
		case 's':			ReadComment(fin);							break;
		case 'v':			ReadVertexDescriptions(fin, isRHCoordSys);	break;
		case 'g':			ReadGroup(fin, groupIndexStart, groupCount);break;
		case 'f':			ReadFace(fin, isRHCoordSys, groupIndexStart, groupCount); break;
		case 'm':			ReadMtlLib(fin);							break;
		case 'u':			ReadUseMtl(fin);							break;
		default:			break;
		}
	}

	// There won't be another index start after our last group, so set it here.
	groupIndexStart.push_back(mVIndex);

	// Sometimes "g" is defined at the very top of the file, then again before
	// the first group of faces. This makes sure the first group does not contain
	// "0" indices.
	if (groupIndexStart[1] == 0)
	{
		groupIndexStart.erase(groupIndexStart.begin()+1);
		groupCount--;
	}

	// Make sure we have a default for the tex coord and normal if one or both
	// are not specified.
	if (!mHasNorm)
		mVertexNormals.push_back(XMFLOAT3(0.0f, 0.0f, 0.0f));
	if (!mHasTexCoord)
		mVertexTextureCoordinates.push_back(XMFLOAT2(0.0f, 0.0f));

	// Close the obj file, and open the mtl file(s).
	fin.close();

	// Loop through every material library.
	for (UINT i = 0; i < mMeshMatLibs.size(); ++i)
	{
		fin.open(mMeshMatLibs[i].c_str());

		if (!fin)
			continue;

		// Read and store all lines so we can handle them individually.
		std::string currentLine = "";
		std::vector<std::string> lines;
		while (!fin.eof())
		{
			getline(fin, currentLine);
			lines.push_back(currentLine);
		}

		fin.close();

		// For each line, make an istringstream out of it and get the first token
		// of the string. Depending on this token, do what needs to be done.
		std::istringstream currentLineSS;
		std::string token;
		for (UINT i = 0; i < lines.size(); ++i)
		{
			currentLineSS = istringstream(lines[i]);
			token = "";
			currentLineSS >> token;
			currentLineSS.get(); // Remove space so methods can do their thing.

				 if (token == "newmtl")	ReadNewMtl(currentLineSS, materials);
			else if (token == "illum")	ReadIllum(currentLineSS, materials);
			else if (token == "Kd")		ReadDiffuseColor(currentLineSS, materials);
			else if (token == "Ka")		ReadAmbientColor(currentLineSS, materials);
			else if (token == "Tf")		ReadTransmissionFilter(currentLineSS, materials);
			else if (token == "map_Kd")	ReadDiffuseMap(currentLineSS, materials);
			else if (token == "Ni")		ReadOpticalDensity(currentLineSS, materials);
			else						continue;
		}
	}

	// Bind each group to it's material. Loop through each group, and compare it큦
	// material name (stored in mGroupMaterials) with each material name stored
	// in MatName of the materials vector. If a match is found, the index to the
	// array of materials is stored in groupMaterialArray, effectively tracking
	// what material each group uses.
	for (UINT i = 0; i < groupCount; ++i)
	{
		bool hasMat = false;
		for (UINT j = 0; j < materials.size(); ++j)
		{
			if (mGroupMaterials[i] == materials[j].MatName)
			{
				materialToUseForGroup.push_back(j);
				j = materials.size(); // Exit inner loop.
				hasMat = true;
			}
		}
		if (!hasMat)
			materialToUseForGroup.push_back(0); // Use first material in array.
	}

	mCompleteVertices = std::vector<Vertex>( );
	Vertex tempVert;

	// Create our vertices using the information we got from the file and store
	// them in a vector.
	for (int j = 0; j < mTotalVertices; ++j)
	{
		tempVert.Position = mVertexPositions[mVertexPositionIndices[j]];
		tempVert.TexCoord = mVertexTextureCoordinates[mVertexTexCoordIndices[j]];
		tempVert.Normal = mVertexNormals[mVertexNormalIndices[j]];

		mCompleteVertices.push_back(tempVert);
	}

	*vertexData = mCompleteVertices.data();
	*indexData = &mIndices[0];
	vertexDataSize = sizeof(Vertex) * mTotalVertices;
	nIndices = mMeshTriangles * 3;
	indexSize = sizeof(DWORD);

	return true;
}

// -----------------------------------------------------------------------------
// Ignores a line by reading chars until end of line.
// -----------------------------------------------------------------------------
void OBJLoader::ReadComment(std::ifstream& fin)
{
	while(mCheckChar != '\n')
		mCheckChar = fin.get();
}

// -----------------------------------------------------------------------------
// Reads a vertex description (v for position, vt for tex coord, vn for normal)
// and adds it to the correct vector. In addition it handles if the model was
// created in a right-handed coordinate system or not.
// -----------------------------------------------------------------------------
void OBJLoader::ReadVertexDescriptions(std::ifstream& fin, bool isRHCoordSys)
{
	mCheckChar = fin.get();
	if (mCheckChar == ' ') // v - vertex position
	{
		float vx, vy, vz;
		fin >> vx >> vy >> vz; // Store the next three types.

		if (isRHCoordSys) // If model is from a RH Coord System, invert z-axis.
			mVertexPositions.push_back(XMFLOAT3(vx, vy, vz * -1.0f));
			//mVertexPositions.push_back(XMFLOAT3(vx, vz, vy));
		else
			mVertexPositions.push_back(XMFLOAT3(vx, vy, vz));
	}
	else if (mCheckChar == 't') // vt - vertex texture coordinate
	{
		float vu, vv;
		fin >> vu >> vv; // Store next two types.

		if (isRHCoordSys) // Reverse v-axis.
			mVertexTextureCoordinates.push_back(XMFLOAT2(vu, 1.0f-vv));
		else
			mVertexTextureCoordinates.push_back(XMFLOAT2(vu, vv));

		mHasTexCoord = true; // We know the model uses texture coordinates.
	}
	else if (mCheckChar == 'n') // vn - vertex normal
	{
		float vx, vy, vz;
		fin >> vx >> vy >> vz; // Store next three types.

		if (isRHCoordSys) // Invert z-axis.
			mVertexNormals.push_back(XMFLOAT3(vx, vy, vz * -1.0f));
			//mVertexNormals.push_back(XMFLOAT3(vx, vz, vy));
		else
			mVertexNormals.push_back(XMFLOAT3(vx, vy, vz));

		mHasNorm = true; // We know the model defines normals.
	}
}

// -----------------------------------------------------------------------------
// Creates a group by storing what index this group begins at.
// -----------------------------------------------------------------------------
void OBJLoader::ReadGroup(std::ifstream& fin, std::vector<UINT>& groupIndexStart, UINT& groupCount)
{
	mCheckChar = fin.get();
	if (mCheckChar == ' ')
	{
		groupIndexStart.push_back(mVIndex); // Start index for this group
		groupCount++;

		// Groups can have names, therefore we read until the end of line.
		// One could save groupnames here if necessary.
		while (mCheckChar != '\n')
			mCheckChar = fin.get();
	}
}

// -----------------------------------------------------------------------------
// Reads an entire line defining a face, adding vertices, checking for duplicates
// as well as retriangulating.
// -----------------------------------------------------------------------------
void OBJLoader::ReadFace(std::ifstream& fin, bool isRHCoordSys, std::vector<UINT>& groupIndexStart, UINT& groupCount)
{
	mCheckChar = fin.get();

	// If the next character by any chance does not separate token and data
	// we stop right away.
	if (mCheckChar != ' ')
		return;

	std::string face = ""; // Contains face vertices.
	int triangleCount = 0; // Used for retriangulating.

	// Read and store the entire line, keeping track of how many triangles it
	// consists of.
	while (mCheckChar != '\n')
	{
		face += mCheckChar; // Add the char to our face string.
		mCheckChar = fin.get(); // Get next character.
		if (mCheckChar == ' ') // If it's a space...
			triangleCount++; // ...increase our triangle count.
	}

	// If the face string is empty there's nothing to parse.
	if (face.length() <= 0)
		return;

	// The first triangle will read 2 spaces, so we need to subtract 1 to make
	// sure that it counts as one triangle. After that, any additional vertex
	// is an additional triangle since it uses previous vertices.
	triangleCount--;

	// Check for space at the end of our face string. Each space is a new triangle
	// but there큦 no new vertex.
	if (face[face.length()-1] == ' ')
		triangleCount--;

	// Now that the line has been read, it's time to disect it into the individual
	// vertices. Each vertex string is parsed to find the indices of the elements
	// it consists of.
	istringstream ss(face);
	std::string vertDef[3]; // Holds the first three vertex definitions.
	int firstVIndex = 0, lastVIndex = 0; // Holds the first and last vertice큦 index.
	
	// Extract the vertex definition (pos/texcoord/norm)
	if (isRHCoordSys)
		ss >> vertDef[2] >> vertDef[1] >> vertDef[0];
	else
		ss >> vertDef[0] >> vertDef[1] >> vertDef[2];

	// Loop through the first three vertices and parse their vertex definitions.
	for (UINT i = 0; i < 3; ++i)
	{
		ParseVertexDefinition(vertDef[i]);

		// This vertex has now been parsed, check to make sure there is at least
		// one group for this to be part of.
		if (groupCount == 0)
		{
			groupIndexStart.push_back(mVIndex); // Start index for this group
			groupCount++;
		}

		// Avoid duplicate vertices
		HandleVertexDuplication();

		// If this is the very first vertex in the face, we need to make sure
		// the rest of the triangles use this vertex.
		if (i == 0)
		{
			firstVIndex = mIndices[mVIndex]; // First vertex index of this face.
		}

		// If this was the last vertex in the first triangle, we will make sure
		// the next triangle uses this one (eg. tri1(1,2,3) tri2(1,3,4) tri3(1,4,5))
		if (i == 2)
		{
			lastVIndex = mIndices[mVIndex]; // Last vertex index of this triangle.
		}

		mVIndex++; // Increment index count.
	}

	mMeshTriangles++; // One triangle down.

	// If there are more than three vertices in the face definition, we need
	// to make sure we convert the face to triangles. We created our first triangle
	// above, now we will create a new triangle for every new vertex in the face,
	// using the very first vertex of the face, and the last vertex from the triangle
	// before the current triangle. Note that this might not work for concave
	// faces, and must be taken care of appropriately.
	RetriangulateFace(firstVIndex, lastVIndex, triangleCount, ss);
}

// -----------------------------------------------------------------------------
// Handles vertex duplication by checking if the current vertex in temporary
// memory already exists in the vertex arrays. If it does, we just reuse the
// index, otherwise we add it.
// -----------------------------------------------------------------------------
void OBJLoader::HandleVertexDuplication()
{
	bool vertAlreadyExists = false;
	if (mTotalVertices >= 3) // Make sure we at least have one triangle to check.
	{
		// Loop through all the vertices.
		for (int iCheck = 0; iCheck < mTotalVertices; ++iCheck)
		{
			// If the vertex position and texture coordinate in memory are the same
			// as the vertex position and texture coordinate we just got out of the
			// obj file, we will set this face큦 vertex index to the index of the
			// vertex already stored. This makes sure we don't store vertices twice.
			if (mVertexPositionIndexTemp == mVertexPositionIndices[iCheck] && !vertAlreadyExists)
			{
				if (mVertexTexCoordIndexTemp == mVertexTexCoordIndices[iCheck])
				{
					// If we've made it here, the vertex already exists.
					vertAlreadyExists = true;
					mIndices.push_back(iCheck); // Set index for this vertex.
					iCheck = mTotalVertices; // Do this to exit loop nicely.
				}
			}
		}
	}

	// If this vertex is not already in our vertex arrays, put it there.
	if (!vertAlreadyExists)
	{
		mVertexPositionIndices.push_back(mVertexPositionIndexTemp);
		mVertexTexCoordIndices.push_back(mVertexTexCoordIndexTemp);
		mVertexNormalIndices.push_back(mVertexNormalIndexTemp);
		mTotalVertices++; // New vertex created, add to total vertices.
		mIndices.push_back(mTotalVertices-1); // Set index for this vertex.
	}
}

// -----------------------------------------------------------------------------
// Converts a face consisting of more than three vertices to triangles.
// Assumes that the first triangle has already been parsed, and the first
// index of that triangle along with the last one is passed to the function.
// -----------------------------------------------------------------------------
void OBJLoader::RetriangulateFace(int firstVIndex, int lastVIndex, int triangleCount, istringstream& ss)
{
	// Loop through the next vertices to create new triangles. Note that we
	// subtract one because one triangle has been handled already!
	for (int l = 0; l < triangleCount-1; l++)
	{
		// First vertex (the very first vertex of the face too)
		mIndices.push_back(firstVIndex); // Set index for this vertex.
		mVIndex++;

		// Second vertex (the last vertex used in the tri before this one)
		mIndices.push_back(lastVIndex); // Set index for this vertex.
		mVIndex++;
		
		std::string vertDef; // Holds one vertex definition at a time.
		ss >> vertDef;
		ParseVertexDefinition(vertDef);

		// Check for duplicate vertices
		HandleVertexDuplication();

		// Set the second vertex for the next triangle to the last vertex we got.
		lastVIndex = mIndices[mVIndex]; // The last vertex index of this triangle.

		mMeshTriangles++; // New triangle defined.
		mVIndex++;
	}
}

// -----------------------------------------------------------------------------
// Parses a vertex definition and sets the temporary values for vertex position,
// texture coordinate, and normal.
// -----------------------------------------------------------------------------
void OBJLoader::ParseVertexDefinition(std::string vertDef)
{
	std::string vertPart; // Part of the vertex definition, e.g. position index.
	int whichPart = 0; // Position, texcoord, or normal. Increments after every parsed index.

	// Parse the individual indices of the vertex definition by finding the
	// divider "/" or last character of the string. When one of those is
	// found, everything read up until now (an integer) is parsed as an
	// integer and stored in a temp variable.
	for (UINT j = 0; j < vertDef.length(); ++j) // Loop through each character.
	{
		// If this isn't a divider "/", add a char to our vertPart.
		if (vertDef[j] != '/')
			vertPart += vertDef[j];

		// If the current char is a divider "/", or it's the last character
		// in the string, it's time to store the part as an integer value.
		if (vertDef[j] == '/' || j == vertDef.length() - 1)
		{
			istringstream stringToInt(vertPart); // Used to convert string to int.

			switch (whichPart)
			{
				// If position
			case 0:
				stringToInt >> mVertexPositionIndexTemp; // Store the index.
				mVertexPositionIndexTemp--; // C++ arrays start with 0, obj starts with 1.

				// If we're at the last index of the string, it means that
				// position was the only thing defined.
				if (j == vertDef.length()-1)
				{
					mVertexTexCoordIndexTemp = 0;
					mVertexNormalIndexTemp = 0;
				}
				break;

				// If tex coord
			case 1:
				// Check to see if there even is a tex coord. An empty string
				// means that another divider was found immediately, which in
				// turn means that a texcoord was not defined.
				if (vertPart != "")
				{
					stringToInt >> mVertexTexCoordIndexTemp; // Store index.
					mVertexTexCoordIndexTemp--; // Zero-based.
				}
				else // If there is no tex coord, make a default
				{
					mVertexTexCoordIndexTemp = 0;
				}

				// If the current char is the last in the string, then
				// there must be no normal, so set a default one.
				if (j == vertDef.length()-1)
				{
					mVertexNormalIndexTemp = 0;
				}
				break;

				// If normal
			case 2:
				stringToInt >> mVertexNormalIndexTemp; // Store index.
				mVertexNormalIndexTemp--; // Zero-based.
				break;
			}

			vertPart = ""; // Get ready for next vertex part.
			whichPart++; // Move on to next vertex part.
		}
	}
}

// -----------------------------------------------------------------------------
// Reads material filename.
// -----------------------------------------------------------------------------
void OBJLoader::ReadMtlLib(std::ifstream& fin)
{
	std::string token = "";

	while(mCheckChar != ' ')
	{
		token += mCheckChar;
		mCheckChar = fin.get();
	}

	// If the token is correct, store the material library큦 filename.
	if (token == "mtllib")
	{
		std::string filename = "";
		fin >> filename;
		mMeshMatLibs.push_back(filename);
	}
}

// -----------------------------------------------------------------------------
// Reads what material to use.
// -----------------------------------------------------------------------------
void OBJLoader::ReadUseMtl(std::ifstream& fin)
{
	std::string token = "";

	while(mCheckChar != ' ')
	{
		token += mCheckChar;
		mCheckChar = fin.get();
	}

	if (token == "usemtl")
	{
		mMeshMaterialsTemp = ""; // Make sure this is cleared.
		fin >> mMeshMaterialsTemp; // Get next type (string)
		mGroupMaterials.push_back(mMeshMaterialsTemp);
	}
}

// -----------------------------------------------------------------------------
// Stores illumination model.
// -----------------------------------------------------------------------------
void OBJLoader::ReadIllum(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	ss >> materials[materials.size()-1].Illum;
}

// -----------------------------------------------------------------------------
// Stores diffuse color for last material.
// -----------------------------------------------------------------------------
void OBJLoader::ReadDiffuseColor(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	//ss >> materials[materials.size()-1].Diffuse.x;
	//ss >> materials[materials.size()-1].Diffuse.y;
	//ss >> materials[materials.size()-1].Diffuse.z;
}

// -----------------------------------------------------------------------------
// Stores ambient color for last material.
// -----------------------------------------------------------------------------
void OBJLoader::ReadAmbientColor(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	ss >> materials[materials.size()-1].Ambient.x;
	ss >> materials[materials.size()-1].Ambient.y;
	ss >> materials[materials.size()-1].Ambient.z;
}

// -----------------------------------------------------------------------------
// Stores transmission filter for last material.
// -----------------------------------------------------------------------------
void OBJLoader::ReadTransmissionFilter(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	ss >> materials[materials.size()-1].TransmissionFilter.x;
	ss >> materials[materials.size()-1].TransmissionFilter.y;
	ss >> materials[materials.size()-1].TransmissionFilter.z;
}

// -----------------------------------------------------------------------------
// Reads a texture map and checks if it already exists. If it does, the material
// reuses the already loaded one, if not, a ShaderResourceView is created and used.
// -----------------------------------------------------------------------------
void OBJLoader::ReadDiffuseMap(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	std::string fileNamePath;

	// Get the file path - We read the pathname char by char since
	// pathnames can sometimes contain spaces, so we will read until
	// we find the file extension.
	bool textFilePathEnd = false;
	while (!textFilePathEnd)
	{
		mCheckChar = ss.get();

		fileNamePath += mCheckChar;

		if (mCheckChar == '.')
		{
			// Assumes a three character long extension.
			for (UINT i = 0; i < 3; ++i)
				fileNamePath += ss.get();

			textFilePathEnd = true;
		}
	}

	materials[materials.size()-1].DiffuseTexture = fileNamePath;
}

// -----------------------------------------------------------------------------
// Stores optical density.
// -----------------------------------------------------------------------------
void OBJLoader::ReadOpticalDensity(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	ss >> materials[materials.size()-1].OpticalDensity;
}

// -----------------------------------------------------------------------------
// Creates a new material. Reads the name of material and stores it.
// -----------------------------------------------------------------------------
void OBJLoader::ReadNewMtl(std::istringstream& ss, std::vector<SurfaceMaterial>& materials)
{
	// New material, set it큦 defaults.
	SurfaceMaterial tempMat;
	ss >> tempMat.MatName;
	materials.push_back(tempMat);
}