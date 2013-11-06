#ifndef GNOMEIMPORTER_H
#define GNOMEIMPORTER_H
#pragma once
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stdlib.h>
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

	struct keyframe
	{
		double time;
		double position[3];
		double scale[3];
		double quaternion[4];
	};

	struct joint
	{
		int parent;
		double offsetMatrix[4][4];
	};
	bool getVectors(string path, vector<material> &materialList, vector<vertex> &vertexList, vector<int>&);
	gnomeImporter(void);
	~gnomeImporter(void);
private:
	int importMaterial(fstream &file, string lines, material &mat);
	int importVertex(fstream &file, string lines, vertex &vet, int &index);
	void importFile(string path);
	bool	SerializeToFile(std::string path);
	bool	DeserializeFromFile(ifstream* file);
public:
	char path[256];
	char scenePath[256];
private:
	vector<material> materials;
	vector<vertex> vertices;
	vector<joint> joints;
	vector<int> turrets; //WAFFLE
	bool headSkeletal, headAnimation;
	static const size_t bufferSize	= 8388608;
};

#endif GNOMEIMPORTER_H

