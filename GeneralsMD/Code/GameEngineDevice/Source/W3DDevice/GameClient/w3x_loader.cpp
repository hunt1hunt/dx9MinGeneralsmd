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
 *                     $Archive::                                                             $*
 *                                                                                             *
 *                       Author:: hzc                                                          *
 *                                                                                             *
 *                    $Modtime::                                                             $*
 *                                                                                             *
 *                   $Revision::                                                             $*
 *---------------------------------------------------------------------------------------------*
 * Functions:                                                                                  *
 *   W3XLoader::ReadFileContent -- read file content via FileClass                             *
 *   W3XLoader::ReadMeshData -- parse .w3x XML into W3XMeshData                               *
 *   W3XLoader::ParseVertices -- parse <Vertices> section                                      *
 *   W3XLoader::ParseNormals -- parse <Normals> section                                        *
 *   W3XLoader::ParseTangents -- parse <Tangents> section                                      *
 *   W3XLoader::ParseBinormals -- parse <Binormals> section                                    *
 *   W3XLoader::ParseTexcoords -- parse <TexCoords> section                                    *
 *   W3XLoader::ParseTriangles -- parse <Triangles> section                                    *
 *   W3XLoader::ParseBoneInfluences -- parse <BoneInfluences> section                          *
 *   W3XLoader::ParseConstants -- parse <Constants> section                                    *
 *   W3XLoader::BuildMesh -- build MeshModelClass from W3XMeshData                            *
 *   W3XLoader::LoadMesh -- load .w3x directly into MeshModelClass                            *
 *   W3XLoader::ParseContainer -- parse W3DContainer XML                                       *
 *   W3XLoader::ParseHierarchy -- parse W3DHierarchy XML                                       *
 *   W3XLoader::ResolveTextureDDS -- resolve texture XML to DDS path                          *
 *   W3XLoader::LogMeshSummary -- log parsed mesh diagnostics                                  *
 *                                                                                             *
 * - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#include "always.h"
#include "w3x_loader.h"

// pugixml configuration: disable STL streams (not available with VC6/STLport)
#define PUGIXML_NO_STL
#include "pugixml.hpp"
#include "ffactory.h"
#include "rawfile.h"
#include "meshmdl.h"
#include "meshgeometry.h"
#include "wwdebug.h"
#include "wwmemlog.h"

//=============================================================================
// Constants
//=============================================================================

static const char *W3X_VERSION = "W3XLoader v1.0 / pugixml 1.16";

// XML namespace strings used in W3X files
// pugixml does not process namespaces, so element names are stored as-is

//=============================================================================
// W3XLoader::Get_Version_String
//=============================================================================
const char *W3XLoader::Get_Version_String(void)
{
	return W3X_VERSION;
}


//=============================================================================
// W3XLoader::ReadFileContent
// Read the entire file into a heap buffer using the engine FileClass.
// Caller must delete[] the returned buffer.
//=============================================================================
char *W3XLoader::ReadFileContent(const char *filename, int &fileSize)
{
	fileSize = 0;

	FileClass *file = _TheFileFactory->Get_File(filename);
	if (!file) {
		DEBUG_LOG(("[W3X_P2] ReadFileContent: Get_File('%s') failed\n", filename));
		return NULL;
	}

	if (!file->Is_Available()) {
		DEBUG_LOG(("[W3X_P2] ReadFileContent: '%s' not available\n", filename));
		_TheFileFactory->Return_File(file);
		return NULL;
	}

	file->Open();
	fileSize = file->Size();
	if (fileSize <= 0) {
		DEBUG_LOG(("[W3X_P2] ReadFileContent: '%s' empty\n", filename));
		file->Close();
		_TheFileFactory->Return_File(file);
		return NULL;
	}

	char *buffer = new char[fileSize + 1];
	if (!buffer) {
		file->Close();
		_TheFileFactory->Return_File(file);
		return NULL;
	}

	int bytesRead = file->Read(buffer, fileSize);
	buffer[bytesRead] = 0;	// null-terminate for XML parser

	file->Close();
	_TheFileFactory->Return_File(file);

	return buffer;
}


//=============================================================================
// W3XLoader::ReadMeshData
// Parse a .w3x mesh XML file and fill W3XMeshData with all content.
//=============================================================================
bool W3XLoader::ReadMeshData(const char *filename, W3XMeshData &data)
{
	WWMEMLOG(MEM_GEOMETRY);

	int fileSize = 0;
	char *xmlBuffer = ReadFileContent(filename, fileSize);
	if (!xmlBuffer) {
		DEBUG_LOG(("[W3X_P2] ReadMeshData: failed to read '%s'\n", filename));
		return false;
	}

	// Parse XML with pugixml
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(xmlBuffer, fileSize,
		pugi::parse_default | pugi::parse_trim_pcdata);

	delete[] xmlBuffer;

	if (!result) {
		DEBUG_LOG(("[W3X_P2] ReadMeshData: XML parse error in '%s': %s (offset %d)\n",
			filename, result.description(), result.offset));
		return false;
	}

	// Navigate: AssetDeclaration > W3DMesh
	pugi::xml_node assetDecl = doc.child("AssetDeclaration");
	if (!assetDecl) {
		// try root-level W3DMesh directly (some files may omit the wrapper)
		assetDecl = doc;
	}

	pugi::xml_node meshNode = assetDecl.child("W3DMesh");
	if (!meshNode) {
		DEBUG_LOG(("[W3X_P2] ReadMeshData: no <W3DMesh> element in '%s'\n", filename));
		return false;
	}

	// Read attributes
	data.id = meshNode.attribute("id").value();
	data.geometryType = meshNode.attribute("GeometryType").value();
	data.castShadow = meshNode.attribute("CastShadow").as_bool(true);

	DEBUG_LOG(("[W3X_P2] W3XLoader::ReadMeshData('%s')\n", filename));
	DEBUG_LOG(("[W3X_P2]   id=%s GeometryType=%s CastShadow=%s\n",
		data.id.str(), data.geometryType.str(), data.castShadow ? "true" : "false"));

	// BoundingBox
	pugi::xml_node bbox = meshNode.child("BoundingBox");
	if (bbox) {
		pugi::xml_node minNode = bbox.child("Min");
		if (minNode) {
			data.boundMin[0] = minNode.attribute("X").as_float();
			data.boundMin[1] = minNode.attribute("Y").as_float();
			data.boundMin[2] = minNode.attribute("Z").as_float();
		}
		pugi::xml_node maxNode = bbox.child("Max");
		if (maxNode) {
			data.boundMax[0] = maxNode.attribute("X").as_float();
			data.boundMax[1] = maxNode.attribute("Y").as_float();
			data.boundMax[2] = maxNode.attribute("Z").as_float();
		}
	}

	// BoundingSphere
	pugi::xml_node bsphere = meshNode.child("BoundingSphere");
	if (bsphere) {
		data.boundSphereRadius = bsphere.attribute("Radius").as_float();
		pugi::xml_node center = bsphere.child("Center");
		if (center) {
			data.boundSphereCenter[0] = center.attribute("X").as_float();
			data.boundSphereCenter[1] = center.attribute("Y").as_float();
			data.boundSphereCenter[2] = center.attribute("Z").as_float();
		}
	}

	// Parse geometry sections
	if (meshNode.child("Vertices")) {
		if (!ParseVertices(meshNode.child("Vertices"), data)) return false;
	}
	if (meshNode.child("Normals")) {
		if (!ParseNormals(meshNode.child("Normals"), data)) return false;
	}
	if (meshNode.child("Tangents")) {
		if (!ParseTangents(meshNode.child("Tangents"), data)) return false;
	}
	if (meshNode.child("Binormals")) {
		if (!ParseBinormals(meshNode.child("Binormals"), data)) return false;
	}
	if (meshNode.child("TexCoords")) {
		if (!ParseTexcoords(meshNode.child("TexCoords"), data)) return false;
	}
	if (meshNode.child("Triangles")) {
		if (!ParseTriangles(meshNode.child("Triangles"), data)) return false;
	}
	if (meshNode.child("BoneInfluences")) {
		if (!ParseBoneInfluences(meshNode.child("BoneInfluences"), data)) return false;
	}

	// FXShader
	pugi::xml_node fxshader = meshNode.child("FXShader");
	if (fxshader) {
		data.fxShaderName = fxshader.attribute("ShaderName").value();
		data.techniqueIndex = fxshader.attribute("TechniqueIndex").as_int(0);
		// Parse constants
		pugi::xml_node constants = fxshader.child("Constants");
		if (constants) {
			ParseConstants(constants, data);
		}
		DEBUG_LOG(("[W3X_P2]   FXShader: %s (technique %d, %d constants)\n",
			data.fxShaderName.str(), data.techniqueIndex, (int)data.constants.size()));
	}

	// Fill default surface types if none provided
	if (data.surfaceTypes.empty() && !data.triangles.empty()) {
		int triCount = (int)data.triangles.size() / 3;
		data.surfaceTypes.resize(triCount, 0);
	}

	LogMeshSummary(filename, data);
	return true;
}


//=============================================================================
// Parse a <Vertices> section: <V X= Y= Z=/>
//=============================================================================
bool W3XLoader::ParseVertices(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;
	for (pugi::xml_node v = node.child("V"); v; v = v.next_sibling("V")) {
		Vector3 vert;
		vert.X = v.attribute("X").as_float();
		vert.Y = v.attribute("Y").as_float();
		vert.Z = v.attribute("Z").as_float();
		data.vertices.push_back(vert);
		count++;
	}
	DEBUG_LOG(("[W3X_P2]   Vertices: %d\n", count));
	return (count > 0);
}


//=============================================================================
// Parse a <Normals> section: <N X= Y= Z=/>
//=============================================================================
bool W3XLoader::ParseNormals(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;
	for (pugi::xml_node n = node.child("N"); n; n = n.next_sibling("N")) {
		Vector3 norm;
		norm.X = n.attribute("X").as_float();
		norm.Y = n.attribute("Y").as_float();
		norm.Z = n.attribute("Z").as_float();
		data.normals.push_back(norm);
		count++;
	}
	DEBUG_LOG(("[W3X_P2]   Normals: %d\n", count));
	return (count > 0);
}


//=============================================================================
// Parse a <Tangents> section: <T X= Y= Z=/>
//=============================================================================
bool W3XLoader::ParseTangents(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;
	for (pugi::xml_node t = node.child("T"); t; t = t.next_sibling("T")) {
		Vector3 tan;
		tan.X = t.attribute("X").as_float();
		tan.Y = t.attribute("Y").as_float();
		tan.Z = t.attribute("Z").as_float();
		data.tangents.push_back(tan);
		count++;
	}
	DEBUG_LOG(("[W3X_P2]   Tangents: %d (native!)\n", count));
	return (count > 0);
}


//=============================================================================
// Parse a <Binormals> section: <B X= Y= Z=/>
//=============================================================================
bool W3XLoader::ParseBinormals(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;
	for (pugi::xml_node b = node.child("B"); b; b = b.next_sibling("B")) {
		Vector3 bin;
		bin.X = b.attribute("X").as_float();
		bin.Y = b.attribute("Y").as_float();
		bin.Z = b.attribute("Z").as_float();
		data.binormals.push_back(bin);
		count++;
	}
	DEBUG_LOG(("[W3X_P2]   Binormals: %d (native!)\n", count));
	return (count > 0);
}


//=============================================================================
// Parse a <TexCoords> section: <T X= Y=/>
//=============================================================================
bool W3XLoader::ParseTexcoords(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;
	for (pugi::xml_node t = node.child("T"); t; t = t.next_sibling("T")) {
		Vector2 tc;
		tc.U = t.attribute("X").as_float();
		tc.V = t.attribute("Y").as_float();
		data.texcoords.push_back(tc);
		count++;
	}
	DEBUG_LOG(("[W3X_P2]   TexCoords: %d\n", count));
	return (count > 0);
}


//=============================================================================
// Parse a <Triangles> section: <T><V>i</V><V>j</V><V>k</V><Nrm .../><Dist .../></T>
//=============================================================================
bool W3XLoader::ParseTriangles(pugi::xml_node &node, W3XMeshData &data)
{
	int triIndex = 0;
	for (pugi::xml_node t = node.child("T"); t; t = t.next_sibling("T")) {
		// Read vertex indices
		int idx0 = 0, idx1 = 0, idx2 = 0;
		int childIdx = 0;
		for (pugi::xml_node v = t.child("V"); v; v = v.next_sibling("V"), childIdx++) {
			int val = v.text().as_int(0);
			if (childIdx == 0) idx0 = val;
			else if (childIdx == 1) idx1 = val;
			else if (childIdx == 2) idx2 = val;
		}
		data.triangles.push_back(idx0);
		data.triangles.push_back(idx1);
		data.triangles.push_back(idx2);

		// Read per-triangle normal (optional, stored in plane equations later)
		pugi::xml_node nrm = t.child("Nrm");
		if (nrm) {
			// Plane normal - stored as part of plane equation during BuildMesh
		}

		// Surface type from attributes if present
		// (W3X triangles don't seem to store surface type directly;
		//  we use default 0 for all triangles)

		triIndex++;
	}

	DEBUG_LOG(("[W3X_P2]   Triangles: %d\n", triIndex));
	return (triIndex > 0);
}


//=============================================================================
// Parse a <BoneInfluences> section: <I Bone="index" Weight="float"/>
//=============================================================================
bool W3XLoader::ParseBoneInfluences(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;
	for (pugi::xml_node i = node.child("I"); i; i = i.next_sibling("I")) {
		int boneIdx = i.attribute("Bone").as_int(0);
		float weight = i.attribute("Weight").as_float(1.0f);
		data.boneIndices.push_back((uint16)boneIdx);
		data.boneWeights.push_back(weight);
		count++;
	}
	DEBUG_LOG(("[W3X_P2]   BoneInfluences: %d\n", count));
	return (count > 0);
}


//=============================================================================
// Parse FXShader <Constants> section
//=============================================================================
bool W3XLoader::ParseConstants(pugi::xml_node &node, W3XMeshData &data)
{
	int count = 0;

	for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
		W3XShaderConstant constant;
		constant.name = c.attribute("Name").value();
		constant.type = W3X_CONSTANT_UNKNOWN;
		constant.floatValue = 0.0f;
		constant.boolValue = false;
		constant.intValue = 0;
		constant.vecSize = 0;

		const char *elemName = c.name();

		if (strcmp(elemName, "Texture") == 0) {
			constant.type = W3X_CONSTANT_TEXTURE;
			constant.textureValue = c.child("Value").text().as_string("");
		} else if (strcmp(elemName, "Float") == 0) {
			constant.type = W3X_CONSTANT_FLOAT;
			constant.floatValue = c.child("Value").text().as_float(0.0f);
		} else if (strcmp(elemName, "Bool") == 0) {
			constant.type = W3X_CONSTANT_BOOL;
			constant.boolValue = c.child("Value").text().as_bool(false);
		} else if (strcmp(elemName, "Int") == 0) {
			constant.type = W3X_CONSTANT_INT;
			constant.intValue = c.child("Value").text().as_int(0);
		} else if (strcmp(elemName, "Vector") == 0) {
			constant.type = W3X_CONSTANT_VECTOR;
			pugi::xml_node val = c.child("Value");
			constant.vecValue[0] = val.attribute("X").as_float(0.0f);
			constant.vecValue[1] = val.attribute("Y").as_float(0.0f);
			constant.vecValue[2] = val.attribute("Z").as_float(0.0f);
			constant.vecValue[3] = val.attribute("W").as_float(0.0f);
			constant.vecSize = val.attribute("W") ? 4 : (val.attribute("Z") ? 3 : 2);
		}

		data.constants.push_back(constant);
		count++;
	}

	return (count > 0);
}


//=============================================================================
// W3XLoader::BuildMesh
// Build a MeshModelClass from parsed W3X mesh data.
//=============================================================================
bool W3XLoader::BuildMesh(const W3XMeshData &data, MeshModelClass *mesh)
{
	if (!mesh) return false;

	int vertCount = (int)data.vertices.size();
	int polyCount = (int)data.triangles.size() / 3;

	if (vertCount == 0 || polyCount == 0) {
		DEBUG_LOG(("[W3X_P2] BuildMesh: empty mesh (%d verts, %d polys)\n",
			vertCount, polyCount));
		return false;
	}

	// Reset the mesh to allocate vertex/polygon arrays (1 pass)
	mesh->Reset(polyCount, vertCount, 1);

	// Set mesh name from W3X id
	mesh->Set_Name(data.id.str());

	// Set bounding box
	Vector3 bmin(data.boundMin[0], data.boundMin[1], data.boundMin[2]);
	Vector3 bmax(data.boundMax[0], data.boundMax[1], data.boundMax[2]);
	mesh->Set_Bounding_Box(bmin, bmax);
	Vector3 bcenter(data.boundSphereCenter[0], data.boundSphereCenter[1], data.boundSphereCenter[2]);
	mesh->Set_Bounding_Sphere(bcenter, data.boundSphereRadius);

	// Copy vertices
	{
		Vector3 *vertArray = mesh->Get_Vertex_Array();
		for (int i = 0; i < vertCount; i++) {
			vertArray[i].X = data.vertices[i].X;
			vertArray[i].Y = data.vertices[i].Y;
			vertArray[i].Z = data.vertices[i].Z;
		}
	}

	// Copy normals (using the new non-const accessor)
	if ((int)data.normals.size() >= vertCount) {
		Vector3 *normArray = mesh->Get_Vertex_Normal_Array(true);
		if (normArray) {
			for (int i = 0; i < vertCount; i++) {
				normArray[i].X = data.normals[i].X;
				normArray[i].Y = data.normals[i].Y;
				normArray[i].Z = data.normals[i].Z;
			}
		}
	}

	// Copy tangents
	if ((int)data.tangents.size() >= vertCount) {
		mesh->Set_Tangent_Array(const_cast<Vector3*>(&data.tangents[0]), vertCount);
	}

	// Copy binormals
	if ((int)data.binormals.size() >= vertCount) {
		mesh->Set_Binormal_Array(const_cast<Vector3*>(&data.binormals[0]), vertCount);
	}

	// Copy triangle indices
	{
		TriIndex *triArray = mesh->Get_Polygon_Array(true);
		for (int i = 0; i < polyCount; i++) {
			triArray[i].I = data.triangles[i * 3 + 0];
			triArray[i].J = data.triangles[i * 3 + 1];
			triArray[i].K = data.triangles[i * 3 + 2];
		}
	}

	// Copy surface types
	{
		uint8 *surfArray = mesh->Get_Poly_Surface_Type_Array();
		if (surfArray && (int)data.surfaceTypes.size() >= polyCount) {
			for (int i = 0; i < polyCount; i++) {
				surfArray[i] = data.surfaceTypes[i];
			}
		}
	}

	// Copy UVs (pass 0, stage 0)
	if (!data.texcoords.empty()) {
		// Build a temporary Vector2 array matching the mesh format
		int uvCount = (int)data.texcoords.size();
		Vector2 *uvArray = new Vector2[uvCount];
		for (int i = 0; i < uvCount; i++) {
			uvArray[i].U = data.texcoords[i].U;
			uvArray[i].V = data.texcoords[i].V;
		}
		mesh->Install_UV_Array(0, 0, uvArray, uvCount);
		delete[] uvArray;
	}

	// Copy bone influences (for skinned meshes)
	if ((int)data.boneIndices.size() >= vertCount) {
		uint16 *boneArray = mesh->Get_Vertex_Bone_Links(true);
		if (boneArray) {
			for (int i = 0; i < vertCount; i++) {
				boneArray[i] = data.boneIndices[i];
			}
		}
		mesh->Set_Flag(MeshGeometryClass::SKIN, true);
	}

	// Set shadow flag
	if (data.castShadow) {
		mesh->Set_Flag(MeshGeometryClass::CAST_SHADOW, true);
	}

	DEBUG_LOG(("[W3X_P2] BuildMesh: '%s' built (%d verts, %d polys, %d UVs, %d bones)\n",
		data.id.str(), vertCount, polyCount,
		(int)data.texcoords.size(), (int)data.boneIndices.size()));

	return true;
}


//=============================================================================
// W3XLoader::LoadMesh (into existing MeshModelClass)
//=============================================================================
bool W3XLoader::LoadMesh(const char *filename, MeshModelClass *mesh)
{
	W3XMeshData data;
	if (!ReadMeshData(filename, data)) {
		return false;
	}
	return BuildMesh(data, mesh);
}


//=============================================================================
// W3XLoader::LoadMesh (create new MeshModelClass)
//=============================================================================
MeshModelClass *W3XLoader::LoadMesh(const char *filename)
{
	W3XMeshData data;
	if (!ReadMeshData(filename, data)) {
		return NULL;
	}

	MeshModelClass *mesh = new MeshModelClass;
	if (!mesh) return NULL;

	if (!BuildMesh(data, mesh)) {
		delete mesh;
		return NULL;
	}

	return mesh;
}


//=============================================================================
// W3XLoader::ParseContainer
// Parse a .w3x W3DContainer: <W3DContainer id="..." Hierarchy="...">
//   <SubObject SubObjectID="..." BoneIndex="0">
//     <RenderObject><Mesh>name</Mesh></RenderObject>
//   </SubObject>
// </W3DContainer>
//=============================================================================
bool W3XLoader::ParseContainer(const char *filename,
							   AsciiString &hierarchyName,
							   std::vector<W3XSubObjectInfo> &subObjects)
{
	subObjects.clear();

	int fileSize = 0;
	char *xmlBuffer = ReadFileContent(filename, fileSize);
	if (!xmlBuffer) return false;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(xmlBuffer, fileSize,
		pugi::parse_default | pugi::parse_trim_pcdata);
	delete[] xmlBuffer;

	if (!result) return false;

	pugi::xml_node assetDecl = doc.child("AssetDeclaration");
	if (!assetDecl) assetDecl = doc;

	pugi::xml_node container = assetDecl.child("W3DContainer");
	if (!container) {
		DEBUG_LOG(("[W3X_P2] ParseContainer: no <W3DContainer> in '%s'\n", filename));
		return false;
	}

	AsciiString containerId = container.attribute("id").value();
	hierarchyName = container.attribute("Hierarchy").value();

	DEBUG_LOG(("[W3X_P2] ParseContainer: '%s' Hierarchy=%s\n",
		containerId.str(), hierarchyName.str()));

	int objCount = 0;
	for (pugi::xml_node sub = container.child("SubObject"); sub; sub = sub.next_sibling("SubObject")) {
		W3XSubObjectInfo info;
		info.subObjectID = sub.attribute("SubObjectID").value();
		info.boneIndex = sub.attribute("BoneIndex").as_int(0);
		info.isCollisionBox = false;

		pugi::xml_node renderObj = sub.child("RenderObject");
		if (renderObj) {
			pugi::xml_node meshRef = renderObj.child("Mesh");
			if (meshRef) {
				info.renderObjectName = meshRef.text().as_string("");
				info.isCollisionBox = false;
			}
			pugi::xml_node colBox = renderObj.child("CollisionBox");
			if (colBox) {
				info.renderObjectName = colBox.text().as_string("");
				info.isCollisionBox = true;
			}
		}

		if (!info.renderObjectName.isEmpty()) {
			subObjects.push_back(info);
			objCount++;
		}
	}

	DEBUG_LOG(("[W3X_P2]   SubObjects: %d\n", objCount));
	return (objCount > 0);
}


//=============================================================================
// W3XLoader::ParseHierarchy
// Parse a .w3x W3DHierarchy: <W3DHierarchy id="...">
//   <Pivot Name="..." Parent="index">
//     <Translation X= Y= Z=/>
//     <Rotation X= Y= Z= W=/>
//     <FixupMatrix .../>
//   </Pivot>
// </W3DHierarchy>
//=============================================================================
bool W3XLoader::ParseHierarchy(const char *filename,
							   std::vector<W3XBoneInfo> &bones)
{
	bones.clear();

	int fileSize = 0;
	char *xmlBuffer = ReadFileContent(filename, fileSize);
	if (!xmlBuffer) return false;

	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(xmlBuffer, fileSize,
		pugi::parse_default | pugi::parse_trim_pcdata);
	delete[] xmlBuffer;

	if (!result) return false;

	pugi::xml_node assetDecl = doc.child("AssetDeclaration");
	if (!assetDecl) assetDecl = doc;

	pugi::xml_node hier = assetDecl.child("W3DHierarchy");
	if (!hier) {
		DEBUG_LOG(("[W3X_P2] ParseHierarchy: no <W3DHierarchy> in '%s'\n", filename));
		return false;
	}

	AsciiString id = hier.attribute("id").value();
	DEBUG_LOG(("[W3X_P2] ParseHierarchy: '%s'\n", id.str()));

	int boneCount = 0;
	for (pugi::xml_node pivot = hier.child("Pivot"); pivot; pivot = pivot.next_sibling("Pivot")) {
		W3XBoneInfo bone;
		bone.name = pivot.attribute("Name").value();
		bone.parentIndex = pivot.attribute("Parent").as_int(-1);

		// Default transform
		for (int i = 0; i < 3; i++) bone.translation[i] = 0.0f;
		for (int j = 0; j < 4; j++) bone.rotation[i] = (j == 3) ? 1.0f : 0.0f;
		for (int k = 0; k < 16; k++) bone.fixupMatrix[i] = (k % 5 == 0) ? 1.0f : 0.0f;

		// Translation
		pugi::xml_node trans = pivot.child("Translation");
		if (trans) {
			bone.translation[0] = trans.attribute("X").as_float();
			bone.translation[1] = trans.attribute("Y").as_float();
			bone.translation[2] = trans.attribute("Z").as_float();
		}

		// Rotation (quaternion)
		pugi::xml_node rot = pivot.child("Rotation");
		if (rot) {
			bone.rotation[0] = rot.attribute("X").as_float();
			bone.rotation[1] = rot.attribute("Y").as_float();
			bone.rotation[2] = rot.attribute("Z").as_float();
			bone.rotation[3] = rot.attribute("W").as_float();
		}

		// FixupMatrix (4x4 column-major)
		pugi::xml_node fixup = pivot.child("FixupMatrix");
		if (fixup) {
			bone.fixupMatrix[0]  = fixup.attribute("M00").as_float();
			bone.fixupMatrix[1]  = fixup.attribute("M10").as_float();
			bone.fixupMatrix[2]  = fixup.attribute("M20").as_float();
			bone.fixupMatrix[3]  = fixup.attribute("M30").as_float();
			bone.fixupMatrix[4]  = fixup.attribute("M01").as_float();
			bone.fixupMatrix[5]  = fixup.attribute("M11").as_float();
			bone.fixupMatrix[6]  = fixup.attribute("M21").as_float();
			bone.fixupMatrix[7]  = fixup.attribute("M31").as_float();
			bone.fixupMatrix[8]  = fixup.attribute("M02").as_float();
			bone.fixupMatrix[9]  = fixup.attribute("M12").as_float();
			bone.fixupMatrix[10] = fixup.attribute("M22").as_float();
			bone.fixupMatrix[11] = fixup.attribute("M32").as_float();
			bone.fixupMatrix[12] = fixup.attribute("M03").as_float();
			bone.fixupMatrix[13] = fixup.attribute("M13").as_float();
			bone.fixupMatrix[14] = fixup.attribute("M23").as_float();
			bone.fixupMatrix[15] = fixup.attribute("M33").as_float();
		}

		bones.push_back(bone);
		boneCount++;
	}

	DEBUG_LOG(("[W3X_P2]   Bones: %d\n", boneCount));
	return (boneCount > 0);
}


//=============================================================================
// W3XLoader::ResolveTextureDDS
// Resolve a texture name to a DDS file path via the texture XML declaration.
// Texture XML files are in the same directory as the model, named <texName>.xml.
// Format: <Texture id="texName" File="texName.dds" OutputFormat="DXT5"/>
//=============================================================================
AsciiString W3XLoader::ResolveTextureDDS(const char *texName)
{
	if (!texName || !texName[0]) return AsciiString("");

	// Construct path to texture XML declaration
	char xmlPath[512];
	sprintf(xmlPath, "%s.xml", texName);

	int fileSize = 0;
	char *xmlBuffer = ReadFileContent(xmlPath, fileSize);
	if (!xmlBuffer) {
		// Try with "Textures/" prefix
		sprintf(xmlPath, "Textures/%s.xml", texName);
		xmlBuffer = ReadFileContent(xmlPath, fileSize);
	}
	if (!xmlBuffer) {
		// Try with "Art/Textures/" prefix
		sprintf(xmlPath, "Art/Textures/%s.xml", texName);
		xmlBuffer = ReadFileContent(xmlPath, fileSize);
	}
	if (!xmlBuffer) {
		// Try direct DDS paths. NOTE: the engine's file system prepends
		// "Art/Textures/" (TGA_DIR_PATH) for image files, so return the bare
		// filename only — otherwise the path gets double-prefixed and the
		// file is not found (which produces the magenta missing-texture).
		char ddsOnly[512];
		sprintf(ddsOnly, "%s.dds", texName);
		DEBUG_LOG(("[W3X_P2] ResolveTextureDDS: no XML for '%s', using: %s\n",
			texName, ddsOnly));
		return AsciiString(ddsOnly);
	}

	// Parse the texture XML
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_buffer(xmlBuffer, fileSize,
		pugi::parse_default | pugi::parse_trim_pcdata);
	delete[] xmlBuffer;

	if (!result) {
		DEBUG_LOG(("[W3X_P2] ResolveTextureDDS: XML parse error for '%s'\n", xmlPath));
		return AsciiString("");
	}

	pugi::xml_node assetDecl = doc.child("AssetDeclaration");
	if (!assetDecl) assetDecl = doc;

	pugi::xml_node textureNode = assetDecl.child("Texture");
	if (!textureNode) {
		DEBUG_LOG(("[W3X_P2] ResolveTextureDDS: no <Texture> in '%s'\n", xmlPath));
		return AsciiString("");
	}

	const char *ddsFile = textureNode.attribute("File").value();
	if (!ddsFile || !ddsFile[0]) {
		// Fallback: construct DDS name from id attribute
		ddsFile = textureNode.attribute("id").value();
	}

	AsciiString resultPath;
	if (ddsFile && ddsFile[0]) {
		// NOTE: the engine's file system prepends "Art/Textures/" (TGA_DIR_PATH)
		// for image files, so return the bare filename (or the XML's relative
		// subpath) — do NOT prepend Art/Textures/ ourselves or the file is
		// double-prefixed and not found (magenta missing-texture).
		resultPath = ddsFile;
		DEBUG_LOG(("[W3X_P2] ResolveTextureDDS: '%s' -> '%s'\n", texName, resultPath.str()));
	}

	return resultPath;
}


//=============================================================================
// W3XLoader::LogMeshSummary
// Log a summary of the parsed mesh data for diagnostics.
//=============================================================================
void W3XLoader::LogMeshSummary(const char *filename, const W3XMeshData &data)
{
	DEBUG_LOG(("============================================================\n"));
	DEBUG_LOG(("[W3X_P2] Mesh Summary: %s\n", filename));
	DEBUG_LOG(("[W3X_P2]   ID: %s\n", data.id.str()));
	DEBUG_LOG(("[W3X_P2]   Type: %s\n", data.geometryType.str()));
	DEBUG_LOG(("[W3X_P2]   Vertices: %d\n", (int)data.vertices.size()));
	DEBUG_LOG(("[W3X_P2]   Normals: %d\n", (int)data.normals.size()));
	DEBUG_LOG(("[W3X_P2]   Tangents: %d (native: %s)\n",
		(int)data.tangents.size(), data.tangents.empty() ? "NO" : "YES"));
	DEBUG_LOG(("[W3X_P2]   Binormals: %d (native: %s)\n",
		(int)data.binormals.size(), data.binormals.empty() ? "NO" : "YES"));
	DEBUG_LOG(("[W3X_P2]   TexCoords: %d\n", (int)data.texcoords.size()));
	DEBUG_LOG(("[W3X_P2]   Triangles: %d\n", (int)data.triangles.size() / 3));
	DEBUG_LOG(("[W3X_P2]   Bones: %d\n", (int)data.boneIndices.size()));

	if (!data.fxShaderName.isEmpty()) {
		DEBUG_LOG(("[W3X_P2]   FXShader: %s (technique %d)\n",
			data.fxShaderName.str(), data.techniqueIndex));
		DEBUG_LOG(("[W3X_P2]   Shader Constants: %d\n", data.constants.size()));
		for (int ci = 0; ci < data.constants.size(); ci++) {
			const W3XShaderConstant &c = data.constants[ci];
			const char *typeStr = "?";
			switch (c.type) {
				case W3X_CONSTANT_TEXTURE: typeStr = "Texture"; break;
				case W3X_CONSTANT_FLOAT:   typeStr = "Float";   break;
				case W3X_CONSTANT_BOOL:    typeStr = "Bool";    break;
				case W3X_CONSTANT_INT:     typeStr = "Int";     break;
				case W3X_CONSTANT_VECTOR:  typeStr = "Vector";  break;
				default: break;
			}
			DEBUG_LOG(("[W3X_P2]     %s %s\n", typeStr, c.name.str()));
			if (c.type == W3X_CONSTANT_TEXTURE) {
				DEBUG_LOG(("[W3X_P2]       Value: %s\n", c.textureValue.str()));
			}
		}
	}

	// Validate data consistency
	if (!data.vertices.empty() && !data.normals.empty()) {
		int vertCount = (int)data.vertices.size();
		int normCount = (int)data.normals.size();
		if (vertCount != normCount) {
			DEBUG_LOG(("[W3X_P2]   ** WARNING: vertex count (%d) != normal count (%d) **\n",
				vertCount, normCount));
		}
	}

	DEBUG_LOG(("============================================================\n"));
}
