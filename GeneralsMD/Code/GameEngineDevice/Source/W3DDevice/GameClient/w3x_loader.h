/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/***********************************************************************************************
 ***              C O N F I D E N T I A L  ---  W E S T W O O D  S T U D I O S               ***
 ***********************************************************************************************
 *                                                                                             *
 *                 Project Name : W3XLoader                                                    *
 *                                                                                             *
 *                    $Archive::                                                             $*
 *                                                                                             *
 *                       Author:: hzc                                                          *
 *                                                                                             *
 *                    $Modtime::                                                             $*
 *                                                                                             *
 *                   $Revision::                                                             $*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   W3XLoader::LoadMesh -- load a .w3x mesh into a MeshModelClass                            *
 *   W3XLoader::ParseContainer -- parse .w3x W3DContainer                                     *
 *   W3XLoader::ParseHierarchy -- parse .w3x W3DHierarchy                                     *
 *   W3XLoader::ReadMeshData -- read raw .w3x mesh data                                       *
 *   W3XLoader::BuildMesh -- build MeshModelClass from parsed data                            *
 *   W3XLoader::ResolveTextureDDS -- resolve texture XML to DDS path                          *
 *                                                                                             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#if defined(_MSC_VER)
#pragma once
#endif

#ifndef W3X_LOADER_H
#define W3X_LOADER_H

#include "always.h"
#include "bittype.h"
#include "Common/AsciiString.h"
#include "vector2.h"
#include "vector3.h"
#include <vector>

class MeshModelClass;

// ----------------------------------------------------------------------------
// Data structures for parsed W3X content
// ----------------------------------------------------------------------------

// W3X sub-object info (from W3DContainer > SubObject)
struct W3XSubObjectInfo
{
	AsciiString subObjectID;
	int boneIndex;
	AsciiString renderObjectName;	// mesh or collision box reference
	bool isCollisionBox;
};

// W3X bone/pivot info (from W3DHierarchy > Pivot)
struct W3XBoneInfo
{
	AsciiString name;
	int parentIndex;
	float translation[3];
	float rotation[4];		// quaternion: x, y, z, w
	float fixupMatrix[16];	// column-major 4x4
};

// W3X animation channel: per-bone keyframes from a W3DAnimation.
// One channel covers ONE pivot (bone) with a per-frame quaternion (orientation)
// and/or a per-frame translation. Either list may be empty for that bone.
struct W3XAnimChannel
{
	int pivot;					// bone index (matches W3DHierarchy Pivot order)
	std::vector<float> quatFrames;	// 4 floats per frame (X,Y,Z,W), empty if none
	std::vector<float> transFrames;	// 3 floats per frame, empty if none
	W3XAnimChannel() : pivot(-1) {}
};

// W3X animation (from a W3DAnimation XML element, e.g. *_FIRE.w3x / *_IDLA.w3x).
struct W3XAnimation
{
	AsciiString name;			// animation id
	AsciiString hierarchy;		// skeleton this anim binds to (e.g. ..._SKL)
	int numFrames;
	int frameRate;
	std::vector<W3XAnimChannel> channels;
	W3XAnimation() : numFrames(0), frameRate(30) {}
};

// W3X FX shader constant type
enum W3XConstantType
{
	W3X_CONSTANT_TEXTURE = 0,
	W3X_CONSTANT_FLOAT,
	W3X_CONSTANT_BOOL,
	W3X_CONSTANT_INT,
	W3X_CONSTANT_VECTOR,
	W3X_CONSTANT_UNKNOWN
};

// W3X FX shader constant
struct W3XShaderConstant
{
	W3XConstantType type;
	AsciiString name;
	AsciiString textureValue;	// for TEXTURE type
	float floatValue;			// for FLOAT type
	bool boolValue;				// for BOOL type
	int intValue;				// for INT type
	float vecValue[4];			// for VECTOR type
	int vecSize;				// for VECTOR type (2, 3, or 4)
};

// Parsed W3X mesh data container (holds all data from a single .w3x <W3DMesh>)
struct W3XMeshData
{
	// Zero-init all scalar members. Vectors are empty by default and filled by
	// ReadMeshData; this guards hasSoftBinding (read by the soft-binding render
	// path in later phases) and the bounds/shadow/technique scalars against
	// uninitialized reads.
	W3XMeshData()
	{
		castShadow = false;
		boundSphereRadius = 0.0f;
		techniqueIndex = 0;
		hasSoftBinding = false;
		boundMin[0] = boundMin[1] = boundMin[2] = 0.0f;
		boundMax[0] = boundMax[1] = boundMax[2] = 0.0f;
		boundSphereCenter[0] = boundSphereCenter[1] = boundSphereCenter[2] = 0.0f;
	}

	AsciiString id;
	AsciiString geometryType;	// "Normal" or "Skin"
	bool castShadow;

	// Bounds
	float boundMin[3];
	float boundMax[3];
	float boundSphereCenter[3];
	float boundSphereRadius;

	// Vertex data
	std::vector<Vector3> vertices;
	std::vector<Vector3> normals;
	std::vector<Vector3> tangents;		// may be empty if not in file
	std::vector<Vector3> binormals;		// may be empty if not in file
	std::vector<Vector2> texcoords;

	// Triangle indices (3 per triangle)
	std::vector<uint32> triangles;

	// Per-triangle surface types (default 0)
	std::vector<uint8> surfaceTypes;

	// Skinning data (per-vertex, one bone index + weight)
	std::vector<uint16> boneIndices;
	std::vector<float> boneWeights;
	// RA3 infantry soft binding: a second per-vertex position/normal and a
	// second bone influence (Position[1]/Normal[1]/BlendIndices.y/BlendWeights.y).
	// Each position/normal is RELATIVE to its bound bone. hasSoftBinding is set
	// when boneIndices2 is non-empty (the mesh is soft-bound infantry).
	std::vector<Vector3> vertices2;
	std::vector<Vector3> normals2;
	std::vector<uint16> boneIndices2;
	std::vector<float> boneWeights2;
	bool hasSoftBinding;



	// FXShader reference
	AsciiString fxShaderName;
	int techniqueIndex;
	std::vector<W3XShaderConstant> constants;
};


// ----------------------------------------------------------------------------
// W3XLoader class
// ----------------------------------------------------------------------------

namespace pugi {
	class xml_node;
}

class W3XLoader
{
public:
	// Get version string
	static const char *Get_Version_String(void);

	// Read raw W3X mesh data from a .w3x file (no MeshModelClass dependency)
	static bool ReadMeshData(const char *filename, W3XMeshData &data);

	// Load a .w3x mesh file and populate an existing MeshModelClass
	static bool LoadMesh(const char *filename, MeshModelClass *mesh);

	// Load a .w3x mesh and create a new MeshModelClass
	static MeshModelClass *LoadMesh(const char *filename);

	// Build MeshModelClass from parsed W3X mesh data
	static bool BuildMesh(const W3XMeshData &data, MeshModelClass *mesh);

	// Parse a .w3x container (W3DContainer) and return sub-object list
	static bool ParseContainer(const char *filename,
		AsciiString &hierarchyName,
		std::vector<W3XSubObjectInfo> &subObjects);

	// Parse a .w3x hierarchy (W3DHierarchy) and return bone list
	static bool ParseHierarchy(const char *filename,
		std::vector<W3XBoneInfo> &bones);

	// Parse a .w3x animation (W3DAnimation) into per-bone keyframes.
	static bool ParseAnimation(const char *filename, W3XAnimation &anim);

	// Resolve a texture name to DDS path via XML declaration file
	static AsciiString ResolveTextureDDS(const char *texName);

	// Log a summary of parsed mesh data (DEBUG_LOG)
	static void LogMeshSummary(const char *filename, const W3XMeshData &data);

private:
	// Internal: read file content into a buffer via the engine FileClass
	static char *ReadFileContent(const char *filename, int &fileSize);

	// Internal: parse XML attributes into our data structures
	static bool ParseVertices(pugi::xml_node &node, W3XMeshData &data, bool second = false);
	static bool ParseNormals(pugi::xml_node &node, W3XMeshData &data, bool second = false);
	static bool ParseTangents(pugi::xml_node &node, W3XMeshData &data);
	static bool ParseBinormals(pugi::xml_node &node, W3XMeshData &data);
	static bool ParseTexcoords(pugi::xml_node &node, W3XMeshData &data);
	static bool ParseTriangles(pugi::xml_node &node, W3XMeshData &data);
	static bool ParseBoneInfluences(pugi::xml_node &node, W3XMeshData &data, bool second = false);
	static bool ParseConstants(pugi::xml_node &node, W3XMeshData &data);

	// Internal: compute tangent/binormal when the .w3x file provides none
	// (Lengyel method over position+texcoord+triangle index). Needed so the
	// RA3 PBR shader's bump-normal perturbation has a TBN to work with.
	static void ComputeFallbackTangents(W3XMeshData &data);

	// Internal: parse one ChannelQuaternion/ChannelTranslation element's frames
	static void ParseAnimationChannel(pugi::xml_node &node, W3XAnimation &anim, bool isQuat);
	// Internal: parse one ChannelScalar element (X/Y/ZTranslation axis) and
	// merge its per-frame values into the pivot's transFrames (3 floats/frame).
	static void ParseAnimationScalar(pugi::xml_node &node, W3XAnimation &anim);
};


#endif /* W3X_LOADER_H */
