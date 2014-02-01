#include "OBJLoader.h"
#include <fstream>
#include <sstream>

using namespace std;

OBJLoader::OBJLoader() : mCurrentMaterial( -1 ), mTotalVertices( 0 )
{
}

OBJLoader::~OBJLoader()
{
	for (int i = 0; i < mMaterials.size(); ++i)
	{
		delete mMaterials[i].name;
	}
}

// Missing feature: Materials. If wanted, one could read mtllib to store all
// material files used. After the .obj file is parsed, all material files can
// be parsed, storing material info such as texture in the materials created
// earlier (create a new one if non-existant).
// TODO:
// Abusement test: Right handed and left handed. RH/LH with negative faces.
// RH/LH with > 3 polygon vertices. RH/LH with > 3 polygon vertices and negative faces.
// 
bool OBJLoader::LoadOBJ( const char *filename, bool rightHanded )
{
	// Open file for read.
	ifstream fin( filename );

	if (!fin)
		return false;

	char curChar;
	while(fin)
	{
		curChar = fin.get(); // Get next char.

		switch (curChar)
		{
		case 'v':			ReadVertexDescriptions(fin, rightHanded); break;
		case 'f':			ReadFace(fin); break;
		case 'u':			ReadUsemtl(fin); break;
		default:			if (curChar != '\n') ReadComment(fin); break;
		}
	}

	fin.close();

	// Loop through every material library to parse .mtl file here if wanted.

	mCompleteVertices = std::vector<Vertex>( );
	Vertex tempVert;

	// Create our vertices using the information we got from the file and store
	// them in a vector.
	for (int j = 0; j < mPositionIndices.size(); ++j)
	{
		tempVert.Position = mPositions[mPositionIndices[j]];
		tempVert.TexCoord = mTexCoords[mTexCoordIndices[j]]; // If not found it's 0
		mCompleteVertices.push_back(tempVert);
	}
	
	if (mNormals.size())
	{
		for (int j = 0; j < mPositionIndices.size(); ++j)
			mCompleteVertices[j].Normal = mNormals[mNormalIndices[j]];
	}
	else // No normals specified, we generate them.
	{
		for (int j = 0; j < mPositionIndices.size(); j += 3)
		{
			XMVECTOR v0 = XMLoadFloat3(&mPositions[mPositionIndices[j  ]]);
			XMVECTOR v1 = XMLoadFloat3(&mPositions[mPositionIndices[j+1]]);
			XMVECTOR v2 = XMLoadFloat3(&mPositions[mPositionIndices[j+2]]);

			XMVECTOR normal;
			if (rightHanded) // No LH calculation because winding is reversed below?
				normal = XMVector3Normalize(XMVector3Cross(v2 - v0, v1 - v0));
			else 
				normal = XMVector3Normalize(XMVector3Cross(v1 - v0, v2 - v0));

			XMStoreFloat3(&mCompleteVertices[j].Normal, normal);
			XMStoreFloat3(&mCompleteVertices[j+1].Normal, normal);
			XMStoreFloat3(&mCompleteVertices[j+2].Normal, normal);

			mNormalIndices[j] = mNormalIndices[j+1] = mNormalIndices[j+2] = j;
		}
	}

	// For every material, add an entry to mBatches, indicating where that batch
	// would start getting indices. Then insert all indices for that material
	// into a common array of indices as well as clear the old one.
	UINT start = 0;
	for (int i = 0; i < mMaterials.size(); ++i)
	{
		std::vector<UINT> &indices = mMaterials[i].indices;
		mBatches.push_back(start);
		mCompleteIndices.insert(mCompleteIndices.end(), indices.begin(), indices.end());
		start += indices.size();

		indices.clear();
	}

	// The last element is not the start of a new batch, but rather used to
	// get how many indices to draw.
	mBatches.push_back(start);

	if (rightHanded) // Reverse winding order
	{
		int temp;
		for (int i = 0; i < mCompleteIndices.size(); i += 3)
		{
			temp = mCompleteIndices[i];
			mCompleteIndices[i] = mCompleteIndices[i+2];
			mCompleteIndices[i+2] = temp;
		}
	}

	return true;
}

// -----------------------------------------------------------------------------
// Reads what material to use.
// -----------------------------------------------------------------------------
// TODO: Right now, all materials have their own list of indices that will later
// be assembled into a complete list of indices. Instead of adding to this list
// for every vertex (ReadFace()), we know that until we stumble upon the next
// usemtl line all faces are using the previously read material. That is, adding
// a start value into a common list of indices to the current material as well as
// current - start we know where in the common list the material has indices (start)
// and how many (current - start). Then we set assign current to start and continue
// using the material we are now using. Note that later the elements of the common
// array would need to be rearranged so that all vertices for a particular material
// are in order. Also note that after the file is read we need to add the last
// entries (because we won't see another usemtl but vertices have been added for
// the last material).
void OBJLoader::ReadUsemtl(std::ifstream &fin)
{
	fin.unget();

	char line[71];
	fin.getline(line, 71);
	
	// Get the material name
	char materialName[64];
	sscanf_s(line, "usemtl %s", materialName, 64);

	int matIndex = GetMaterial(materialName);

	// If the material does not already exist, we add it.
	if (matIndex == -1)
	{
		Material mat;
		mat.name = new char[65];
		strcpy(mat.name, materialName);

		mMaterials.push_back(mat);
		matIndex = mMaterials.size() - 1;
	}

	mCurrentMaterial = matIndex;
}

// --------------------------------------------------------------------------
// Returns the index to the materials array for a certain material, or -1 if
// the material name wasn't found.
// --------------------------------------------------------------------------
int OBJLoader::GetMaterial(const char *name)
{
	for (int i = 0; i < mMaterials.size(); ++i)
	{
		if (strcmp(name, mMaterials[i].name) == 0) // Equal
			return i;
	}

	return -1;
}

// -----------------------------------------------------------------------------
// Ignores a line by reading chars until end of line.
// -----------------------------------------------------------------------------
void OBJLoader::ReadComment(std::ifstream& fin)
{
	std::string dump;
	getline(fin, dump);
}

// -----------------------------------------------------------------------------
// Parse and store vertex data (position, tex coord or normal).
// -----------------------------------------------------------------------------
void OBJLoader::ReadVertexDescriptions(std::ifstream& fin, bool rightHanded)
{
	float x, y, z;

	switch (fin.get())
	{
	case ' ': // v - position
		fin >> x >> y >> z;

		if (rightHanded) // Invert z
			z = -z;

		mPositions.push_back(XMFLOAT3(x, y, z));
		break;
	case 't': // vt - texture coordinate
		fin >> x >> y;

		if (rightHanded) // Reverse v-axis
			y = 1.0f - y;

		mTexCoords.push_back(XMFLOAT2(x, y));
		break;
	case 'n': // vn - normal
		fin >> x >> y >> z;

		if (rightHanded) // Invert z
			z = -z;

		mNormals.push_back(XMFLOAT3(x, y, z));
		break;
	}

	fin.get(); // \n
}

// -----------------------------------------------------------------------------
// Reads an entire line defining a face, adding vertices, checking for duplicates
// as well as retriangulating.
// -----------------------------------------------------------------------------
void OBJLoader::ReadFace(std::ifstream& fin)
{
	char curChar = fin.get();

	// If the next character by any chance does not separate token and data
	// we stop right away.
	if (curChar != ' ')
		return;

	std::string face = ""; // Contains face vertices.
	int triangleCount = 0; // Used for retriangulating.

	// Read and store the entire line, keeping track of how many triangles it
	// consists of.
	while (curChar != '\n')
	{
		face += curChar; // Add the char to our face string.
		curChar = fin.get(); // Get next character.
		if (curChar == ' ') // If it's a space...
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
	// but there´s no new vertex.
	if (face[face.length()-1] == ' ')
		triangleCount--;

	// Now that the line has been read, it's time to disect it into the individual
	// vertices. Each vertex string is parsed to find the indices of the elements
	// it consists of.
	istringstream ss(face);
	std::string vertDef[3]; // Holds the first three vertex definitions.
	int firstVIndex = 0, lastVIndex = 0; // Holds the first and last vertice´s index.
	
	ss >> vertDef[0] >> vertDef[1] >> vertDef[2];

	// Loop through the first three vertices and parse their vertex definitions.
	for (UINT i = 0; i < 3; ++i)
	{
		UINT position, texCoord, normal;
		ParseVertexDefinition(vertDef[i], position, texCoord, normal);

		// TODO: Here could be checked that if this vertex setup already exists
		// we loop through all mPositionIndices (and texcoord indices and normal
		// indices at the same time, they have the same amount of elements),
		// and if they contain the same indices (mPositionIndices[i] == position)
		// we don't add entries to those arrays and instead just reuse the index
		// in the common array (commonIndices.push_back(i)). This way we don't
		// recreate vertices that already exists but rather reuse them, resulting
		// in a smaller vertex buffer.

		// När man lägger till en vertexpunkt (värden till mPositionIndices osv) ökar vertex-
		// talet. Detta vertextal ska läggas in i materialets index array. Eftersom
		// vertextalet kommer motsvara index till vertexpunkten som skapas senare kommer
		// materialets array hålla indexvärden till kompletta vertexarrayen, alltså vilka
		// vertexpunkter som ska renderas för det givna materialet.
		mPositionIndices.push_back(position);
		mTexCoordIndices.push_back(texCoord);
		mNormalIndices.push_back(normal);
		mMaterials[mCurrentMaterial].indices.push_back(mTotalVertices++);

		// If this is the very first vertex in the face, we need to make sure
		// the rest of the triangles use this vertex.
		if (i == 0 )
		{
			firstVIndex = mPositionIndices.size()-1;
		}
	}

	// If there are more than three vertices in the face definition, we need
	// to make sure we convert the face to triangles. We created our first triangle
	// above, now we will create a new triangle for every new vertex in the face,
	// using the very first vertex of the face, and the last vertex from the triangle
	// before the current triangle. Note that this might not work for concave
	// faces, and must be taken care of appropriately.
	RetriangulateFace(firstVIndex, triangleCount, ss);
}

// -----------------------------------------------------------------------------
// Converts a face consisting of more than three vertices to triangles.
// Assumes that the first triangle has already been parsed, and the first
// index of that triangle along with the last one is passed to the function.
// -----------------------------------------------------------------------------
// TODO: This method isn't really required. It works basically the same as in
// ReadFace, except that the first two vertices of this triangle are reused from
// the ones defined earlier. This could easily be incorporated in ReadFace by
// setting first vertex index to mPositionIndices.size() BEFORE any vertices
// are added. Then the face declaration is simply read until there are no more
// vertex definitions. Every vertex with loop index greater than 2 (4th vertex
// or more) is added exactly the same, but also reuses two vertices, exactly as
// is done in this method. This also removes anything that has to do with trianglecount.
void OBJLoader::RetriangulateFace(int firstVIndex, int triangleCount, std::istringstream& ss)
{
	// Loop through the next vertices to create new triangles. Note that we
	// subtract one because one triangle has been handled already!
	for (int l = 0; l < triangleCount-1; l++)
	{
		// Reuse the first vertex of the face (copy the indices of that vertex)
		mPositionIndices.push_back(mPositionIndices[firstVIndex]);
		mTexCoordIndices.push_back(mTexCoordIndices[firstVIndex]);
		mNormalIndices.push_back(mNormalIndices[firstVIndex]);
		mMaterials[mCurrentMaterial].indices.push_back(mTotalVertices++);

		// Reuse the last vertex of the previous triangle (size - 2 because
		// size - 1 is the one we just added for this triangle!)
		mPositionIndices.push_back(mPositionIndices[mPositionIndices.size()-2]);
		mTexCoordIndices.push_back(mTexCoordIndices[mTexCoordIndices.size()-2]);
		mNormalIndices.push_back(mNormalIndices[mNormalIndices.size()-2]);
		mMaterials[mCurrentMaterial].indices.push_back(mTotalVertices++);
		
		std::string vertDef; // Holds one vertex definition at a time.
		ss >> vertDef;
		UINT position, texCoord, normal;
		ParseVertexDefinition(vertDef, position, texCoord, normal);

		mPositionIndices.push_back(position);
		mTexCoordIndices.push_back(texCoord);
		mNormalIndices.push_back(normal);
		mMaterials[mCurrentMaterial].indices.push_back(mTotalVertices++);
	}
}

// -----------------------------------------------------------------------------
// Parses a vertex definition and sets the temporary values for vertex position,
// texture coordinate, and normal.
// -----------------------------------------------------------------------------
void OBJLoader::ParseVertexDefinition(std::string vertDef, UINT &position, UINT &texCoord, UINT &normal)
{
	// Read the indices of the vertex definition.
	int ptn[] = { 0, 0, 0 };
	int n = 0;
	const char *loc = vertDef.c_str();
	for (int i = 0; i < 3; ++i) // Three indices in a vertdef
	{
		// We only store in one variable (number of characters read n does not
		// increase assignment count), therefore the return value can be used
		// as a success value, indicating a value was read or not.
		int success = sscanf_s(loc, "%i%n", &ptn[i], &n);
		loc += success * n + 1; // Increase pointer by number of read chars + 1 for slash.
		if (loc - vertDef.c_str() >= strlen(vertDef.c_str())) // Parsed entire string? Early break.
			break;
	}

	// Position index
	if (ptn[0] > 0)
		position = ptn[0] - 1;
	else
		position = mPositions.size() + ptn[0];

	// Tex coord index
	if (ptn[1] > 0)
		texCoord = ptn[1] - 1;
	else if (ptn[1] < 0)
		texCoord = mTexCoords.size() + ptn[1];
	else
		texCoord = 0;

	// Normal index (if zero, we keep it like that)
	if (ptn[2] > 0)
		normal = ptn[2] - 1;
	else if (ptn[2] < 0)
		normal = mNormals.size() + ptn[2];
	else
		normal = 0;
}