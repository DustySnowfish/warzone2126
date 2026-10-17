// SPDX-License-Identifier: GPL-2.0-or-later

/*
	This file is part of Warzone 2126, based on Warzone 2100.
	Copyright (C) 2026  Warzone 2126 contributors

	Warzone 2126 is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	Warzone 2126 is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with Warzone 2126; if not, write to the Free Software
	Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
*/
/** \file
 *  glTF 2.0 display-model import. See gltfload.h.
 */

// cgltf is third-party C code; keep its warnings out of the strict CI build.
#if defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wcast-align"
#  pragma clang diagnostic ignored "-Wcast-qual"
#  pragma clang diagnostic ignored "-Wold-style-cast"
#  pragma clang diagnostic ignored "-Wsign-compare"
#  pragma clang diagnostic ignored "-Wunused-parameter"
#  pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#  pragma clang diagnostic ignored "-Wshorten-64-to-32"
#  pragma clang diagnostic ignored "-Wcomma"
#  pragma clang diagnostic ignored "-Wdouble-promotion"
#  pragma clang diagnostic ignored "-Wextra-semi-stmt"
#  pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wcast-align"
#  pragma GCC diagnostic ignored "-Wcast-qual"
#  pragma GCC diagnostic ignored "-Wold-style-cast"
#  pragma GCC diagnostic ignored "-Wsign-compare"
#  pragma GCC diagnostic ignored "-Wunused-parameter"
#  pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#  pragma GCC diagnostic ignored "-Wdouble-promotion"
#  pragma GCC diagnostic ignored "-Wuseless-cast"
#  pragma GCC diagnostic ignored "-Wnull-dereference"
#elif defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#define CGLTF_IMPLEMENTATION
#include "3rdparty/cgltf.h"
#if defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#  pragma warning(pop)
#endif

#include "gltfload.h"

#include "lib/framework/frame.h"
#include "lib/framework/physfs_ext.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace
{

constexpr const char *OVERRIDES_FILE = "assets/model_overrides.json";
constexpr size_t MAX_POLYS = 21844;   // floor((UINT16_MAX - 1) / 3): the PIE loader's 16-bit index limit
constexpr size_t MAX_CONNECTORS = 16;

bool overridesLoaded = false;
std::unordered_map<std::string, GLTFOverride> overrides;

std::string toLower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool readPhysFSFile(const std::string &path, std::vector<char> &out)
{
	PHYSFS_file *f = PHYSFS_openRead(path.c_str());
	if (!f)
	{
		return false;
	}
	const PHYSFS_sint64 len = PHYSFS_fileLength(f);
	if (len < 0)
	{
		PHYSFS_close(f);
		return false;
	}
	out.resize(static_cast<size_t>(len));
	const bool ok = len == 0 || WZ_PHYSFS_readBytes(f, out.data(), static_cast<PHYSFS_uint32>(len)) == len;
	PHYSFS_close(f);
	return ok;
}

void loadOverrides()
{
	overridesLoaded = true;
	overrides.clear();
	if (!PHYSFS_exists(OVERRIDES_FILE))
	{
		return;
	}
	std::vector<char> data;
	if (!readPhysFSFile(OVERRIDES_FILE, data))
	{
		debug(LOG_ERROR, "Could not read %s", OVERRIDES_FILE);
		return;
	}
	nlohmann::json doc = nlohmann::json::parse(data.begin(), data.end(), nullptr, false);
	if (doc.is_discarded() || !doc.is_object() || !doc.contains("overrides") || !doc["overrides"].is_object())
	{
		debug(LOG_ERROR, "%s is not valid (expected an \"overrides\" object)", OVERRIDES_FILE);
		return;
	}
	for (const auto &item : doc["overrides"].items())
	{
		const nlohmann::json &v = item.value();
		GLTFOverride o;
		if (v.is_string())
		{
			o.modelPath = v.get<std::string>();
		}
		else if (v.is_object() && v.contains("model") && v["model"].is_string())
		{
			o.modelPath = v["model"].get<std::string>();
			if (v.contains("scale") && v["scale"].is_number())
			{
				o.scale = v["scale"].get<float>();
			}
		}
		else
		{
			debug(LOG_ERROR, "%s: invalid entry for %s", OVERRIDES_FILE, item.key().c_str());
			continue;
		}
		overrides[toLower(item.key())] = o;
	}
	debug(LOG_INFO, "Loaded %zu glTF model overrides", overrides.size());
}

struct Vec3
{
	float x = 0.f, y = 0.f, z = 0.f;
};

Vec3 transformPoint(const float m[16], const float p[3])
{
	// glTF matrices are column-major
	Vec3 r;
	r.x = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
	r.y = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
	r.z = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
	return r;
}

Vec3 transformNormal(const float m[16], const float n[3])
{
	// Uses the upper 3x3 directly; fine for the rotation + uniform scale that exports produce.
	Vec3 r;
	r.x = m[0] * n[0] + m[4] * n[1] + m[8] * n[2];
	r.y = m[1] * n[0] + m[5] * n[1] + m[9] * n[2];
	r.z = m[2] * n[0] + m[6] * n[1] + m[10] * n[2];
	const float len = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z);
	if (len > 1e-8f)
	{
		r.x /= len; r.y /= len; r.z /= len;
	}
	return r;
}

/// True for LOD1, LOD2, ... nodes, which are skipped (only LOD0 is imported for now).
bool isLowerLOD(const cgltf_node *node)
{
	if (!node->name)
	{
		return false;
	}
	const std::string name = toLower(node->name);
	return name.size() > 3 && name.rfind("lod", 0) == 0 && std::isdigit(static_cast<unsigned char>(name[3])) && name != "lod0";
}

struct ImportedMesh
{
	std::vector<Vec3> positions;           // PIE space, scaled
	std::vector<Vec3> normals;             // PIE space, per vertex
	std::vector<std::pair<float, float>> uvs;
	std::vector<uint32_t> indices;         // triangles, PIE winding
	const cgltf_material *material = nullptr;
	std::map<size_t, Vec3> connectors;     // index -> PIE connector space, scaled
};

/// "conn_turret_2" -> 1, "conn_muzzle_1" -> 0; returns false if not a connector name.
bool parseConnectorName(const char *rawName, size_t &indexOut)
{
	if (!rawName)
	{
		return false;
	}
	const std::string name = toLower(rawName);
	if (name.rfind("conn_", 0) != 0)
	{
		return false;
	}
	const size_t us = name.find_last_of('_');
	if (us == std::string::npos || us + 1 >= name.size())
	{
		return false;
	}
	const std::string kind = name.substr(5, us - 5);
	if (kind != "turret" && kind != "muzzle" && kind != "weapon")
	{
		return false;   // conn_propulsion, conn_module_N, conn_exit: not PIE connectors
	}
	const int n = atoi(name.c_str() + us + 1);
	if (n < 1 || static_cast<size_t>(n) > MAX_CONNECTORS)
	{
		return false;
	}
	indexOut = static_cast<size_t>(n - 1);
	return true;
}

bool importNode(const cgltf_node *node, float scale, ImportedMesh &out, std::string &errorOut)
{
	if (isLowerLOD(node))
	{
		return true;
	}

	float world[16];
	cgltf_node_transform_world(node, world);

	size_t connIndex = 0;
	if (parseConnectorName(node->name, connIndex))
	{
		const float origin[3] = {0.f, 0.f, 0.f};
		const Vec3 g = transformPoint(world, origin);
		// glTF (x, y, z) -> PIE connector (x, -z, y)
		Vec3 c;
		c.x = g.x * scale;
		c.y = -g.z * scale;
		c.z = g.y * scale;
		out.connectors[connIndex] = c;
	}

	if (node->mesh)
	{
		for (cgltf_size p = 0; p < node->mesh->primitives_count; ++p)
		{
			const cgltf_primitive &prim = node->mesh->primitives[p];
			if (prim.type != cgltf_primitive_type_triangles)
			{
				continue;
			}
			const cgltf_accessor *posAcc = nullptr, *nrmAcc = nullptr, *uvAcc = nullptr;
			for (cgltf_size a = 0; a < prim.attributes_count; ++a)
			{
				const cgltf_attribute &attr = prim.attributes[a];
				if (attr.type == cgltf_attribute_type_position) { posAcc = attr.data; }
				else if (attr.type == cgltf_attribute_type_normal) { nrmAcc = attr.data; }
				else if (attr.type == cgltf_attribute_type_texcoord && attr.index == 0) { uvAcc = attr.data; }
			}
			if (!posAcc)
			{
				continue;
			}
			if (!uvAcc)
			{
				errorOut = "mesh '" + std::string(node->mesh->name ? node->mesh->name : "?") + "' has no UVs (TEXCOORD_0)";
				return false;
			}
			if (out.material == nullptr)
			{
				out.material = prim.material;
			}
			else if (prim.material != out.material)
			{
				debug(LOG_WARNING, "glTF: more than one material in one model; only the first material's textures are used");
			}

			const uint32_t base = static_cast<uint32_t>(out.positions.size());
			for (cgltf_size v = 0; v < posAcc->count; ++v)
			{
				float pos[3] = {0.f, 0.f, 0.f}, nrm[3] = {0.f, 1.f, 0.f}, uv[2] = {0.f, 0.f};
				cgltf_accessor_read_float(posAcc, v, pos, 3);
				if (nrmAcc)
				{
					cgltf_accessor_read_float(nrmAcc, v, nrm, 3);
				}
				cgltf_accessor_read_float(uvAcc, v, uv, 2);

				const Vec3 g = transformPoint(world, pos);
				const Vec3 gn = transformNormal(world, nrm);
				// glTF (x, y, z) -> PIE (x, y, -z)
				out.positions.push_back({g.x * scale, g.y * scale, -g.z * scale});
				out.normals.push_back({gn.x, gn.y, -gn.z});
				out.uvs.emplace_back(uv[0], uv[1]);   // glTF and PIE both use a top-left UV origin
			}

			const cgltf_size indexCount = prim.indices ? prim.indices->count : posAcc->count;
			for (cgltf_size i = 0; i + 2 < indexCount; i += 3)
			{
				uint32_t tri[3];
				for (int k = 0; k < 3; ++k)
				{
					tri[k] = base + static_cast<uint32_t>(prim.indices ? cgltf_accessor_read_index(prim.indices, i + k) : i + k);
				}
				// The mirror on z flips handedness, so reverse the winding to keep faces front-facing.
				out.indices.push_back(tri[0]);
				out.indices.push_back(tri[2]);
				out.indices.push_back(tri[1]);
			}
		}
	}

	for (cgltf_size c = 0; c < node->children_count; ++c)
	{
		if (!importNode(node->children[c], scale, out, errorOut))
		{
			return false;
		}
	}
	return true;
}

/// Image path relative to the data root, prefixed with '/' so the texture loader
/// does not look for it under texpages/.
std::string resolveImagePath(const std::string &modelDir, const cgltf_texture_view &view)
{
	if (!view.texture || !view.texture->image || !view.texture->image->uri)
	{
		return {};
	}
	std::string uri = view.texture->image->uri;
	if (uri.rfind("data:", 0) == 0)
	{
		return {};   // embedded images are not supported; keep textures as separate PNG files
	}
	std::replace(uri.begin(), uri.end(), '\\', '/');
	return "/" + modelDir + uri;
}

std::string fmtFloat(float v)
{
	if (std::fabs(v) < 1e-6f)
	{
		v = 0.f;
	}
	char buf[32];
	snprintf(buf, sizeof(buf), "%.5g", static_cast<double>(v));
	return buf;
}

} // anonymous namespace

optional<GLTFOverride> gltfFindOverride(const std::string &pieFilename)
{
	if (!overridesLoaded)
	{
		loadOverrides();
	}
	auto it = overrides.find(toLower(pieFilename));
	if (it == overrides.end())
	{
		return nullopt;
	}
	return it->second;
}

void gltfResetOverrides()
{
	overridesLoaded = false;
	overrides.clear();
}

optional<std::string> gltfConvertToPIE(const std::string &modelPath, const std::vector<char> &glbData, float scale, std::string &errorOut, GLTFExtraMaterial &extraOut)
{
	cgltf_options options = {};
	cgltf_data *raw = nullptr;
	if (cgltf_parse(&options, glbData.data(), glbData.size(), &raw) != cgltf_result_success)
	{
		errorOut = "not a valid glTF 2.0 file";
		return nullopt;
	}
	std::unique_ptr<cgltf_data, void (*)(cgltf_data *)> data(raw, cgltf_free);
	if (cgltf_load_buffers(&options, data.get(), nullptr) != cgltf_result_success)
	{
		errorOut = "could not load buffers (use a binary .glb with embedded geometry)";
		return nullopt;
	}
	if (cgltf_validate(data.get()) != cgltf_result_success)
	{
		errorOut = "glTF validation failed";
		return nullopt;
	}

	const cgltf_scene *scene = data->scene ? data->scene : (data->scenes_count > 0 ? &data->scenes[0] : nullptr);
	if (!scene)
	{
		errorOut = "no scene";
		return nullopt;
	}

	ImportedMesh mesh;
	for (cgltf_size n = 0; n < scene->nodes_count; ++n)
	{
		if (!importNode(scene->nodes[n], scale, mesh, errorOut))
		{
			return nullopt;
		}
	}
	if (mesh.indices.empty())
	{
		errorOut = "no triangle geometry found";
		return nullopt;
	}
	const size_t numPolys = mesh.indices.size() / 3;
	if (numPolys > MAX_POLYS)
	{
		errorOut = "too many triangles (" + std::to_string(numPolys) + ", limit " + std::to_string(MAX_POLYS) + ")";
		return nullopt;
	}
	if (!mesh.material || !mesh.material->has_pbr_metallic_roughness)
	{
		errorOut = "no metallic-roughness material";
		return nullopt;
	}

	const size_t slash = modelPath.find_last_of('/');
	const std::string modelDir = (slash == std::string::npos) ? std::string() : modelPath.substr(0, slash + 1);

	const std::string baseColor = resolveImagePath(modelDir, mesh.material->pbr_metallic_roughness.base_color_texture);
	if (baseColor.empty())
	{
		errorOut = "material has no base colour texture (PNG file next to the .glb)";
		return nullopt;
	}
	const std::string normalMap = resolveImagePath(modelDir, mesh.material->normal_texture);
	const std::string ormMap = resolveImagePath(modelDir, mesh.material->pbr_metallic_roughness.metallic_roughness_texture);

	extraOut = GLTFExtraMaterial();
	const std::string emissiveMap = resolveImagePath(modelDir, mesh.material->emissive_texture);
	if (!emissiveMap.empty())
	{
		const float strength = mesh.material->has_emissive_strength ? mesh.material->emissive_strength.emissive_strength : 1.f;
		extraOut.emissiveFile = emissiveMap;
		for (int c = 0; c < 3; ++c)
		{
			extraOut.emissiveFactor[c] = mesh.material->emissive_factor[c] * strength;
		}
	}
	if (ormMap.empty())
	{
		debug(LOG_WARNING, "%s: no metallic-roughness (ORM) texture; using classic shading", modelPath.c_str());
	}

	std::string tcMask;
	if (mesh.material->extras.data)
	{
		nlohmann::json extras = nlohmann::json::parse(mesh.material->extras.data, nullptr, false);
		if (!extras.is_discarded() && extras.is_object() && extras.contains("wz_tcmask") && extras["wz_tcmask"].is_string())
		{
			tcMask = "/" + modelDir + extras["wz_tcmask"].get<std::string>();
		}
	}

	// Build PIE 4 text
	std::string pie;
	pie.reserve(mesh.positions.size() * 40 + numPolys * 140);
	pie += "PIE 4\n";
	unsigned int typeFlags = 0x200;            // iV_IMD_TEX
	if (!tcMask.empty()) { typeFlags |= 0x10000; }   // iV_IMD_TCMASK
	if (!ormMap.empty()) { typeFlags |= 0x100000; }  // iV_IMD_PBR
	// glTF allows occlusion in a separate texture. The engine reads it from the R channel of the
	// metal/roughness texture, so only trust R when the occlusion texture is that same image.
	const cgltf_texture *mrTex = mesh.material->pbr_metallic_roughness.metallic_roughness_texture.texture;
	const cgltf_texture *aoTex = mesh.material->occlusion_texture.texture;
	const bool ormHasOcclusion = mrTex && aoTex && (aoTex == mrTex || (aoTex->image && aoTex->image == mrTex->image));
	if (!ormMap.empty() && !ormHasOcclusion) { typeFlags |= 0x200000; }  // iV_IMD_PBR_NO_AO
	char typeLine[32];
	snprintf(typeLine, sizeof(typeLine), "TYPE %x\n", typeFlags);
	pie += typeLine;
	pie += "TEXTURE 0 " + baseColor + "\n";
	if (!tcMask.empty())
	{
		pie += "TCMASK 0 " + tcMask + "\n";
	}
	if (!normalMap.empty())
	{
		pie += "NORMALMAP 0 " + normalMap + "\n";
	}
	if (!ormMap.empty())
	{
		pie += "SPECULARMAP 0 " + ormMap + "\n";
	}
	pie += "LEVELS 1\nLEVEL 1\n";
	pie += "POINTS " + std::to_string(mesh.positions.size()) + "\n";
	for (const Vec3 &p : mesh.positions)
	{
		pie += "\t" + fmtFloat(p.x) + " " + fmtFloat(p.y) + " " + fmtFloat(p.z) + "\n";
	}
	pie += "NORMALS " + std::to_string(numPolys) + "\n";
	for (size_t t = 0; t < numPolys; ++t)
	{
		pie += "\t";
		for (int k = 0; k < 3; ++k)
		{
			const Vec3 &n = mesh.normals[mesh.indices[t * 3 + k]];
			pie += fmtFloat(n.x) + " " + fmtFloat(n.y) + " " + fmtFloat(n.z) + (k < 2 ? " " : "\n");
		}
	}
	pie += "POLYGONS " + std::to_string(numPolys) + "\n";
	for (size_t t = 0; t < numPolys; ++t)
	{
		const uint32_t a = mesh.indices[t * 3], b = mesh.indices[t * 3 + 1], c = mesh.indices[t * 3 + 2];
		pie += "\t200 3 " + std::to_string(a) + " " + std::to_string(b) + " " + std::to_string(c);
		// Model textures are sampled with clamp-to-edge, but glTF exporters often place
		// UVs in a repeated tile (for example v in 1..2). Shift each triangle back into
		// the 0..1 tile; this is exact for any triangle that does not straddle a tile edge.
		const float minU = std::min({mesh.uvs[a].first, mesh.uvs[b].first, mesh.uvs[c].first});
		const float minV = std::min({mesh.uvs[a].second, mesh.uvs[b].second, mesh.uvs[c].second});
		const float shiftU = std::floor(minU + 1e-4f);
		const float shiftV = std::floor(minV + 1e-4f);
		for (uint32_t idx : {a, b, c})
		{
			pie += " " + fmtFloat(mesh.uvs[idx].first - shiftU) + " " + fmtFloat(mesh.uvs[idx].second - shiftV);
		}
		pie += "\n";
	}
	if (!mesh.connectors.empty())
	{
		const size_t count = mesh.connectors.rbegin()->first + 1;
		pie += "CONNECTORS " + std::to_string(count) + "\n";
		for (size_t i = 0; i < count; ++i)
		{
			auto it = mesh.connectors.find(i);
			const Vec3 c = (it != mesh.connectors.end()) ? it->second : Vec3();
			if (it == mesh.connectors.end())
			{
				debug(LOG_WARNING, "%s: connector %zu missing (connectors should be numbered from 1 without gaps)", modelPath.c_str(), i + 1);
			}
			// The PIE loader reads connectors as integers
			pie += "\t" + std::to_string(std::lround(c.x)) + " " + std::to_string(std::lround(c.y)) + " " + std::to_string(std::lround(c.z)) + "\n";
		}
	}
	return pie;
}
