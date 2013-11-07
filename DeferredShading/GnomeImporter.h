#ifndef GNOMEIMPORTER_H
#define GNOMEIMPORTER_H
#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdlib.h>
#include <Windows.h>
#define WIN32_LEAN_AND_MEAN
using namespace std;

class gnomeImporter
{
public:

struct material
{
	bool hasAlpha;
	double ambient[3];
	double color[3];
	double specularity[3];
	double specularPower;
	double reflectivity;
	double transparancy;
	char name[256];
	char texture[256];
	char normalTexture[256];
	char alphaTexture[256];
};

struct vertex
{
	int material;
	int jointIndex[4];
	double position[3];
	double normal[3];
	double tangent[3];
	double biNormal[3];
	double uv[2];
	double skinWeight[4];
};

	bool getVectors(string path, vector<material> &materialList, vector<vertex> &vertexList, vector<int>&);
	gnomeImporter(void);
	~gnomeImporter(void);
private:
	int importMaterial(fstream &file, string lines, material &mat);
	int importVertex(fstream &file, string lines, vertex &vet, int &index);
	void importFile(string path);
	bool	SerializeToFile(std::string path);
	bool	DeserializeFromFile(std::string binaryPath, size_t fileLength);
public:
	char scenePath[256];
private:
	vector<material> materials;
	vector<vertex> vertices;
	bool headSkeletal, headAnimation;

	//Constants for serialization
	static const size_t bufferSize		= 8388608;
	static const size_t materialSize	= sizeof(material);
	static const size_t vertexSize		= sizeof(vertex);
	static const size_t uintSize		= sizeof(unsigned int);
	static const size_t intSize			= sizeof(int);
};

#endif GNOMEIMPORTER_H

