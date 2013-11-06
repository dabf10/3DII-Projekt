/* dot gnome parser */

#include "gnomeImporter.h"
//#include "Misc.h"



void split(vector<string> &tokens, const string &text, char sep) {
	int start = 0, end = 0;
	while ((end = text.find(sep, start)) != string::npos) {
		tokens.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	tokens.push_back(text.substr(start));
}

/* Fuck you C# notation */
int gnomeImporter::importMaterial(fstream &file, string line, material &theMaterial)
{
	if (line == "A")
		file >> theMaterial.ambient[0] >> theMaterial.ambient[1] >> theMaterial.ambient[2];
	else if ( line == "AC")
		file >> theMaterial.hasAlpha;
	else if ( line == "D") //diffuse or color
		file >> theMaterial.color[0] >> theMaterial.color[1] >> theMaterial.color[2];
	else if ( line == "S")
		file >> theMaterial.specularity[0] >> theMaterial.specularity[1] >> theMaterial.specularity[2];
	else if (line == "SP")
		file >> theMaterial.specularPower;
	else if (line == "R")
		file >> theMaterial.reflectivity;
	else if (line == "T")
		file >> theMaterial.transparancy;
	else if (line == "DM")
	{
		file >> theMaterial.texture;
		if (theMaterial.texture[0] == '0')
			theMaterial.texture[0] = '\0';
	}
	else if (line == "NM")
	{
		file >> theMaterial.normalTexture;
		if (theMaterial.normalTexture[0] == '0')
			theMaterial.normalTexture[0] = '\0';
	}
	else if (line == "AM")
	{
		file >> theMaterial.alphaTexture;
		if (theMaterial.alphaTexture[0] == '0')
			theMaterial.alphaTexture[0] = '\0';
	}
	else
	{
		materials.push_back(theMaterial);
		if (line[0] == '*') // Maybe todo: get out from if's for optimization (?)
			return 1;
		else
			strcpy_s(theMaterial.name, line.length() + 1, line.c_str());
	}
	return 0;
}

int gnomeImporter::importVertex(fstream &file, string line, vertex &theVertex, int &index)
{
	if (line == "%M")
		file >> index;
	else if (line == "P")
	{
		file >> theVertex.position[0] >> theVertex.position[1] >> theVertex.position[2];
	}
	else if (line == "N")
	{
		file >> theVertex.normal[0] >> theVertex.normal[1] >> theVertex.normal[2];
	}
	else if (line == "T")
	{
		file >> theVertex.tangent[0] >> theVertex.tangent[1] >> theVertex.tangent[2];
	}
	else if (line == "BN")
	{
		file >> theVertex.biNormal[0] >> theVertex.biNormal[1] >> theVertex.biNormal[2];
	}
	else if (line == "UV")
	{
		file >> theVertex.uv[0] >> theVertex.uv[1];
		theVertex.uv[1] = 1 - theVertex.uv[1];
		if (!headSkeletal)
		{
			theVertex.material = index;
			vertices.push_back(theVertex);
		}
	}
	else if (line == "BW")
		file >> theVertex.skinWeight[0] >> theVertex.skinWeight[1] >> theVertex.skinWeight[2] >> theVertex.skinWeight[3];
	else if (line == "BI")
	{
		file >> theVertex.jointIndex[0] >> theVertex.jointIndex[1] >> theVertex.jointIndex[2] >> theVertex.jointIndex[3];
		theVertex.material = index;
		vertices.push_back(theVertex);
	}
	else if (line[0] == '*')
		return 1;
	return 0;
}

/* if file dosen't have animations, eof will be reached before animations are written. */
void gnomeImporter::importFile(string path)
{
	int order = 0;

	fstream m_File (path, ios::in);
	material m_Material;
	vertex m_Vertex;

	int m_MatIndex = 0;
	int m_KeyIndex = 0;
	bool m_isSkeletal = false;
	bool m_isAnimated = 0;
	unsigned int m_JointIndex = 0;
	string m_Line;


	char buffer[512] = {0};

	bool wasPushed = false;

	if(m_File)
	{
		int		i[4];
		float	f[16];
		char	c[64];

		while (m_File.good())
		{
			m_File.getline( buffer, sizeof( buffer ) );

			int asterix				= sscanf_s( buffer, "*%s", c, sizeof(c) );
			// *****HEADER*****
			int sceneName			= sscanf_s( buffer, "#S %s", c, sizeof(c) );						// #S
			int numberOfMaterials	= sscanf_s( buffer, "#M %d", &i[0] );								// #M
			int numberOfVertices	= sscanf_s( buffer, "#V %d", &i[0] );								// #V
			// #T
			int isAnimated			= sscanf_s( buffer, "#iA %d", &i[0] );								// #iA
			int isSkeletal			= sscanf_s( buffer, "#iS %d", &i[0] );								// #iS
			// #B
			// #A

			// *****MATERIALS*****
			//int materialName		= sscanf_s( buffer, "%s", c, sizeof(c) );							// <matName> (fix)
			int materialAmbient		= sscanf_s( buffer, "A %f %f %f", &f[0], &f[1], &f[2] );			// A
			int materialDiffuse		= sscanf_s( buffer, "D %f %f %f", &f[0], &f[1], &f[2] );			// D
			int materialSpecular	= sscanf_s( buffer, "S %f %f %f", &f[0], &f[1], &f[2] );			// S
			int materialSpecularPow = sscanf_s( buffer, "SP %f", &f[0] );								// SP
			int materialReflective	= sscanf_s( buffer, "R %f", &f[0] );								// R
			int materialTransparant	= sscanf_s( buffer, "T %f", &f[0] );								// T
			int materialHasAlpha	= sscanf_s( buffer, "AC %d", &i[0] );								// AC
			int materialTexture		= sscanf_s( buffer, "DM %s", c, sizeof(c) );						// DM
			int materialNormalMap	= sscanf_s( buffer, "NM %s", c, sizeof(c) );						// NM
			int materialAlphaTex	= sscanf_s( buffer, "AM %s", c, sizeof(c) );						// AM

			// *****VERTICES*****
			int meshGroup			= sscanf_s( buffer, "%%M %d", &i[0] );								// %M
			int vertexPosition		= sscanf_s( buffer, "P %f %f %f", &f[0], &f[1], &f[2] );			// P
			int vertexNormal		= sscanf_s( buffer, "N %f %f %f", &f[0], &f[1], &f[2] );			// N
			int vertexTangent		= sscanf_s( buffer, "T %f %f %f", &f[0], &f[1], &f[2] );			// T
			int vertexBiNormal		= sscanf_s( buffer, "BN %f %f %f", &f[0], &f[1], &f[2] );			// BN
			int vertexUV			= sscanf_s( buffer, "UV %f %f",	&f[0], &f[1] );						// UV
			int vertexBoneWeight	= sscanf_s( buffer, "BW %f %f %f %f", &f[0], &f[1], &f[2], &f[3] );	// BW
			int vertexBoneIndices	= sscanf_s( buffer, "BI %d %d %d %d", &i[0], &i[1], &i[2], &i[3] );	// BI

			// *****BONEOFFSETS*****
			int jointOffset			= sscanf_s( buffer, "jointOffset%d %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f",
				&i[0], &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6], &f[7],
				&f[8], &f[9], &f[10], &f[11], &f[12], &f[13], &f[14], &f[15] ); // jointOffset

			// *****BONEHIERARCHY*****
			int jointParent			= sscanf_s( buffer, "jointParent%d %d", &i[0], &i[1] );				// jointParent

			// *****BONETURRETID*****
			// DO THIS !!!!!!!!!!!!!!!!!!!!!!!!!!!!!

			// ******ANIMATIONCLIPS*******
			int animationClip		= sscanf_s( buffer, "AnimationClip %s", c, sizeof(c) );				// AnimationClip
			int jointFrames			= sscanf_s( buffer, "\tjoint%d #Keyframes: %d", &i[0], &i[1] );		// joint
			int keyframe = sscanf_s( buffer, "\t\tTime: %f Pos: %f %f %f Scale: %f %f %f Quat: %f %f %f %f",
				&f[0], &f[1], &f[2], &f[3],	&f[4], &f[5], &f[6], &f[7], &f[8], &f[9], &f[10] ); // keyframe



			switch (order)
			{
			case 0:
				order++;
				break;
			case 1:
				if ( asterix > 0 )
				{
					string dafuqName = "SomethingSomethingDarkside";
					strcpy_s(m_Material.name, dafuqName.length() + 1, dafuqName.c_str());
					order++;
				}
				else if ( isSkeletal )
				{
					headSkeletal = m_isSkeletal = (bool)i[0];
				}
				else if ( isAnimated )
				{
					headAnimation = m_isAnimated = (bool)i[0];
				}
				else if ( sceneName > 0 )
				{
					strcpy_s(scenePath, strlen(c) + 1, c);
				}
				break;
			case 2:
				{
					if ( materialAmbient > 0 )
					{
						m_Material.ambient[0] = f[0];
						m_Material.ambient[1] = f[1];
						m_Material.ambient[2] = f[2];
						wasPushed = false;
					}
					else if ( materialHasAlpha > 0 )
						m_Material.hasAlpha = (bool)i[0];
					else if ( materialDiffuse > 0 )
					{
						m_Material.color[0] = f[0];
						m_Material.color[1] = f[1];
						m_Material.color[2] = f[2];
					}
					else if ( materialSpecular > 0 )
					{
						m_Material.specularity[0] = f[0];
						m_Material.specularity[1] = f[1];
						m_Material.specularity[2] = f[2];
					}
					else if ( materialSpecularPow > 0 )
						m_Material.specularPower = f[0];
					else if ( materialReflective > 0 )
						m_Material.reflectivity = f[0];
					else if ( materialTransparant > 0 )
						m_Material.transparancy = f[0];
					else if ( materialTexture > 0 )
					{
						strcpy_s(m_Material.texture, strlen(c) + 1, c);
						if (m_Material.texture[0] == '0')
							m_Material.texture[0] = '\0';
					}
					else if ( materialNormalMap > 0 )
					{
						strcpy_s(m_Material.normalTexture, strlen(c) + 1, c);
						if (m_Material.normalTexture[0] == '0')
							m_Material.normalTexture[0] = '\0';
					}
					else if ( materialAlphaTex > 0 )
					{
						strcpy_s(m_Material.alphaTexture, strlen(c) + 1, c);
						if (m_Material.alphaTexture[0] == '0')
							m_Material.alphaTexture[0] = '\0';
					}
					else if ( m_Material.ambient[0] >= 0 && !wasPushed )
					{
						materials.push_back(m_Material);
						wasPushed = true;
						if ( asterix > 0 )
							order++;
						else
							strcpy_s(m_Material.name, strlen("SomethingSomethingDarkside") + 1, "SomethingSomethingDarkside");
					}
					else if( asterix > 0 )
						order++;
				}
				break;
			case 3:
				{
					if ( meshGroup > 0 )
						m_MatIndex = i[0];
					else if ( vertexPosition > 0 )
					{
						m_Vertex.position[0] = f[0];
						m_Vertex.position[1] = f[1];
						m_Vertex.position[2] = f[2];
					}
					else if ( vertexNormal > 0 )
					{
						m_Vertex.normal[0] = f[0];
						m_Vertex.normal[1] = f[1];
						m_Vertex.normal[2] = f[2];
					}
					else if ( vertexTangent > 0 )
					{
						m_Vertex.tangent[0] = f[0];
						m_Vertex.tangent[1] = f[1];
						m_Vertex.tangent[2] = f[2];
					}
					else if ( vertexBiNormal > 0 )
					{
						m_Vertex.biNormal[0] = f[0];
						m_Vertex.biNormal[1] = f[1];
						m_Vertex.biNormal[2] = f[2];
					}
					else if ( vertexUV > 0 )
					{
						m_Vertex.uv[0] = f[0];
						m_Vertex.uv[1] = f[1];
						m_Vertex.uv[1] = 1 - m_Vertex.uv[1];

						if (!headSkeletal)
						{
							m_Vertex.material = m_MatIndex;
							vertices.push_back( m_Vertex );
						}
					}
					else if ( vertexBoneWeight > 0 )
					{
						m_Vertex.skinWeight[0] = f[0];
						m_Vertex.skinWeight[1] = f[1];
						m_Vertex.skinWeight[2] = f[2];
						m_Vertex.skinWeight[3] = f[3];
					}
					else if ( vertexBoneIndices > 0 )
					{
						m_Vertex.jointIndex[0] = i[0];
						m_Vertex.jointIndex[1] = i[1];
						m_Vertex.jointIndex[2] = i[2];
						m_Vertex.jointIndex[3] = i[3];
						m_Vertex.material = m_MatIndex;
						vertices.push_back( m_Vertex );
					}
					else if ( asterix > 0 )
						order++;
				}
				break;
			case 4:
				if ( asterix > 0 )
					order++;
				else if (m_isSkeletal)
					int i = 0;

				else if (m_isAnimated)
					int i = 0;
				else
					order++;
				break;

				if ( asterix || !m_isSkeletal )
				{
					m_JointIndex = 0;
					order++;
					if (!m_isSkeletal)
						order += 100;
				}

				break;
			case 6:
				if ( asterix > 0 )
					order++;

				break;
			}
		}
		m_File.close();
		SerializeToFile(path);
	}
}

bool gnomeImporter::getVectors(string path, vector<material> &materialList, vector<vertex> &vertexList, vector<int> &materialSwitchIndices)
{
	std::string binaryPath = path + "BINARY";
	ifstream binaryFile(binaryPath, std::ifstream::binary);
	if(binaryFile.is_open())
	{
		binaryFile.seekg (0,binaryFile.end);
		size_t length =binaryFile.tellg();
		binaryFile.seekg (0, binaryFile.beg);
		DeserializeFromFile(binaryPath, length);
	}
	else
	{
		binaryFile.close();
		importFile(path);
	}

	for (unsigned int i = 0; i < materials.size(); i++)
	{
		materialList.push_back(materials[i]);
		for (unsigned int j = 0; j < vertices.size(); j++)
		{
			if (vertices[j].material == i)
				vertexList.push_back(vertices[j]);
		}
		materialSwitchIndices.push_back(vertexList.size());
	}
	return true;
}


//!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! EOF-tecknet kan komma ifrån de inlästa filerna !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!

bool gnomeImporter::SerializeToFile(std::string path)
{
	const size_t materialSize	= sizeof(material);
	const size_t vertexSize		= sizeof(vertex);
	const size_t keyframeSize	= sizeof(keyframe);
	const size_t jointSize		= sizeof(joint);

	char* binary	= new char[bufferSize];
	char* pos		= binary;
	size_t readCount = 0;

	//booleans
	memcpy_s(pos, bufferSize, &headAnimation, 1);
	++pos;
	++readCount;
	memcpy_s(pos, bufferSize, &headSkeletal, 1);
	++pos;
	++readCount;

	//vectors
	unsigned int vectorSize = materials.size();
	memcpy_s(pos, bufferSize, &vectorSize, 4);
	pos += 4;
	readCount += 4;
	for (unsigned int i = 0; i < materials.size(); ++i)
	{
		memcpy_s(pos, bufferSize, &materials[i], materialSize);
		pos += materialSize;
		readCount += materialSize;
	}

	vectorSize = vertices.size();
	memcpy_s(pos, bufferSize, &vectorSize, 4);
	pos += 4;
	readCount += 4;
	for (unsigned int i = 0; i < vertices.size(); ++i)
	{
		memcpy_s(pos, bufferSize, &vertices[i], vertexSize);
		pos += vertexSize;
		readCount += vertexSize;
	}

	vectorSize = joints.size();
	memcpy_s(pos, bufferSize, &vectorSize, 4);
	pos += 4;
	readCount += 4;
	for (unsigned int i = 0; i < joints.size(); ++i)
	{
		memcpy_s(pos, bufferSize, &joints[i], jointSize);
		pos += jointSize;
		readCount += jointSize;
	}

	vectorSize = turrets.size();
	memcpy_s(pos, bufferSize, &vectorSize, 4);
	pos += 4;
	readCount += 4;
	for (unsigned int i = 0; i < turrets.size(); ++i)
	{
		memcpy_s(pos, bufferSize, &turrets[i], 4);
		pos += 4;
		readCount += 4;

	}

	//strings
	memcpy_s(pos, bufferSize, &scenePath, sizeof(scenePath));
	pos += sizeof(scenePath);
	readCount += sizeof(scenePath);
	memcpy_s(pos, bufferSize, &path, sizeof(path));
	pos += sizeof(path);
	readCount += sizeof(path);

	//flush
	ofstream ofs = ofstream(path + "BINARY", ofstream::binary);
	ofs.write(binary, readCount); //Ingen nullterminator
	ofs.close();

	//Cleanup
	if(binary)
		delete[] binary;

	return true;
}

bool gnomeImporter::DeserializeFromFile(std::string binaryPath, size_t fileLength)
{
	const size_t materialSize	= sizeof(material);
	const size_t vertexSize		= sizeof(vertex);
	const size_t keyframeSize	= sizeof(keyframe);
	const size_t jointSize		= sizeof(joint);

	HANDLE fileHandle = CreateFileA(binaryPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	DWORD bytesread = 0; // debug
	char* buffer = new char[fileLength];
	ReadFile(fileHandle, buffer, fileLength, &bytesread, NULL);
		return true;
}

gnomeImporter::gnomeImporter(void)
{
}


gnomeImporter::~gnomeImporter(void)
{
}