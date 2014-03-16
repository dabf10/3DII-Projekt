#include "Model.h"
#include "DXUT.h"

Model::Model() : mVBuffer(0), mIBuffer(0)
{
}

// OBS! Vid tilldelning kommer pekarna för dessa objekt att kopieras över, men
// inte datan de pekar på, och när den temporära Modelvariabeln destrueras
// rensas datan som pekarna pekar på ut och det blir problem. För att lösa det
// kan man göra egna copy constructor, copy assignment operator, och destructor.
// Kom ihåg Rule of Three: Behövs en av dessa, behövs antagligen alla. I detta
// fall används en Initialize, men destructorn körs vid tilldelning icke desto
// mindre eftersom först: Model mModel; och senare mModel = Model(); Konstruktor
// körs två gånger och en tilldelning sker > destruktorn anropas på tempvariabel.
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
	//if(BinaryExists(filename))
	//{
	//	return Model::LoadBinary(std::string(filename), device);
	//}

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

	//Write to binary file
//	SerializeToFile(filename, vertexData, vertexDataSize, indexData, indexSize * indices);

	return true;
}

bool Model::LoadGnome(const char* filename, ID3D11Device* device)
{
	gnomeImporter importer;
	std::vector<gnomeImporter::vertex> vertexList;
	std::vector<gnomeImporter::material> materialList;
	std::vector<int> materialSwitchIndices;

	if(!importer.getVectors(filename, materialList, vertexList, materialSwitchIndices))
		return false;	//This is never going to happen since the importer never returns false >.>

	Vertex* convertedVertices = ConvertVertices(vertexList);
	const void*		vertexData		= convertedVertices;
	unsigned int	vertexDataSize	= sizeof(Vertex) * vertexList.size();

	D3D11_BUFFER_DESC vertexBufferDescription;
	vertexBufferDescription.Usage				= D3D11_USAGE_IMMUTABLE;
	vertexBufferDescription.BindFlags			= D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDescription.CPUAccessFlags		= 0;
	vertexBufferDescription.MiscFlags			= 0;
	vertexBufferDescription.StructureByteStride = 0;
	vertexBufferDescription.ByteWidth = vertexDataSize;

	D3D11_SUBRESOURCE_DATA vertexInitData;
	vertexInitData.pSysMem			= vertexData;
	vertexInitData.SysMemPitch		= 0;
	vertexInitData.SysMemSlicePitch = 0;

	if (FAILED(device->CreateBuffer(&vertexBufferDescription, &vertexInitData, &mVBuffer)))
	{
		delete[] convertedVertices;
		return false;
	}

	int* indexData = new int[vertexList.size()];
	for(int i = 0; i < vertexList.size(); ++i)
	{
		indexData[i] = i;
	}

	// Create index buffer
	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = 4 * vertexList.size();
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	ibd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = indexData;
	if(FAILED(device->CreateBuffer(&ibd, &iinitData, &mIBuffer)))
	{
		delete[] convertedVertices;
		delete[] indexData;
		return false;
	}

	mBatches.push_back(0);
	mBatches.push_back(vertexList.size());

	delete[] convertedVertices;
	delete[] indexData;

	return true;
}

Model::Vertex* Model::ConvertVertices(std::vector<gnomeImporter::vertex> importedVertices)
{
	Vertex* convertedVertices = new Vertex[importedVertices.size()];

	for(int i = 0; i < importedVertices.size(); ++i)
	{
		convertedVertices[i].Position.x = importedVertices[i].position[0];
		convertedVertices[i].Position.y = importedVertices[i].position[1];
		convertedVertices[i].Position.z = importedVertices[i].position[2];

		convertedVertices[i].Normal.x	= importedVertices[i].normal[0];
		convertedVertices[i].Normal.y	= importedVertices[i].normal[1];
		convertedVertices[i].Normal.z	= importedVertices[i].normal[2];

		convertedVertices[i].TexCoord.x = importedVertices[i].uv[0];
		convertedVertices[i].TexCoord.y = importedVertices[i].uv[1];
	}
	return convertedVertices;
}

bool Model::SerializeToFile(std::string fileName, const void* vertices, unsigned int vertexTotalSize, const void* indices, unsigned int indexTotalSize)
{
	unsigned int totalByteSize = vertexTotalSize + indexTotalSize + (sizeof(UINT) * mBatches.size()) + (sizeof(int) * 3);
	char* binary	= new char[totalByteSize];
	char* pos		= binary;
	unsigned int bytesWritten = 0;

	//vertex size
	WriteBinary(pos, totalByteSize, bytesWritten, &vertexTotalSize, sizeof(unsigned int));

	//vertex data
	WriteBinary(pos, totalByteSize, bytesWritten, vertices, vertexTotalSize);

	//index size
	WriteBinary(pos, totalByteSize, bytesWritten, &indexTotalSize, sizeof(unsigned int));

	//index data
	WriteBinary(pos, totalByteSize, bytesWritten, indices, indexTotalSize);

	//Batch count
	unsigned int batchCount = mBatches.size();
	WriteBinary(pos, totalByteSize, bytesWritten, &batchCount, sizeof(unsigned int));

	//Batch data
	UINT* batchData = mBatches.data();
	WriteBinary(pos, totalByteSize, bytesWritten, &batchData, sizeof(UINT) * batchCount);

	if(bytesWritten != totalByteSize)
	{
		delete[] binary;
		return false;
	}

	ofstream ofs = ofstream(fileName + "BINARY", ofstream::binary);
	ofs.write(binary, bytesWritten);
	delete[] binary;
	return true;
}

bool Model::LoadBinary(std::string fileName, ID3D11Device* device)
{
	fileName = fileName + "BINARY";

	ifstream file = ifstream(fileName);
	file.seekg(0, file.end);
	unsigned int totalByteSize = file.tellg();
	file.seekg(0, file.beg);

	HANDLE fileHandle = CreateFileA(fileName.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	char* buffer = new char[totalByteSize];
	char* pos = buffer;
	DWORD bytesReadFromFile = 0;
	ReadFile(fileHandle, buffer, totalByteSize, &bytesReadFromFile, NULL);
	file.close();

	if(bytesReadFromFile != totalByteSize)
		return false;

	unsigned int bytesReadFromBuffer = 0;

	//Copy Data

	//Vertice Size
	unsigned int vertexTotalSize;
	ReadBinary(pos, &vertexTotalSize, sizeof(unsigned int), bytesReadFromBuffer);

	//Vertex Data
	char* vertices = new char[vertexTotalSize];
	ReadBinary(pos, vertices, vertexTotalSize, bytesReadFromBuffer);

	//Indice Size
	unsigned int indexTotalSize;
	ReadBinary(pos, &indexTotalSize, sizeof(unsigned int), bytesReadFromBuffer);

	//Index Data
	char* indices = new char[indexTotalSize];
	ReadBinary(pos, indices, indexTotalSize, bytesReadFromBuffer);

	//Batch Count
	unsigned int batchCount;
	ReadBinary(pos, &batchCount, sizeof(unsigned int), bytesReadFromBuffer);

	//Batch Data
	UINT* batches = new UINT[batchCount];
	ReadBinary(pos, batches, sizeof(UINT) * batchCount, bytesReadFromBuffer);

	if(bytesReadFromBuffer != totalByteSize)
		return  false;

	//Insert Data

	// Create index buffer
	D3D11_BUFFER_DESC ibd;
	ibd.Usage = D3D11_USAGE_IMMUTABLE;
	ibd.ByteWidth = indexTotalSize;
	ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	ibd.CPUAccessFlags = 0;
	ibd.MiscFlags = 0;
	ibd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA iinitData;
	iinitData.pSysMem = indices;
	if(FAILED(device->CreateBuffer(&ibd, &iinitData, &mIBuffer)))
		return false;

	// Create vertex buffer
	D3D11_BUFFER_DESC vbd;
	vbd.Usage = D3D11_USAGE_IMMUTABLE;
	vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vbd.ByteWidth = vertexTotalSize;
	vbd.CPUAccessFlags = 0;
	vbd.MiscFlags = 0;
	vbd.StructureByteStride = 0;

	D3D11_SUBRESOURCE_DATA vinitData;
	vinitData.pSysMem = vertices;
	vinitData.SysMemPitch = 0;
	vinitData.SysMemSlicePitch = 0;
	if(FAILED(device->CreateBuffer(&vbd, &vinitData, &mVBuffer)))
		return false;

	mBatches.insert(mBatches.end(), &batches[0], &batches[batchCount]);

	delete[] vertices;
	delete[] indices;
	delete[] batches;
	return true;
}

void Model::WriteBinary(char*& position, unsigned int bufferSize, unsigned int& bytesWritten, const void* data, unsigned int maxCount)
{
	memcpy_s(position, bufferSize - bytesWritten, data, maxCount);
	position += maxCount;
	bytesWritten += maxCount;
}

void Model::ReadBinary(char*& position, void* data, unsigned int dataSize, unsigned int& bytesRead)
{
	memcpy_s(data, dataSize, position, dataSize);
	position += dataSize;
	bytesRead += dataSize;
}

bool Model::BinaryExists(std::string fileName)
{
	std::string binaryPath = fileName + "BINARY";
	ifstream binaryFile(binaryPath, std::ifstream::binary);
	if(binaryFile.is_open())
	{
		binaryFile.close();
		return true;
	}

	return false; //No need to close, file was never opened.
}