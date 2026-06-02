#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_shape_assets.h"

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/PlaneShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

#include "runtime/physics/physics_asset_common.h"

namespace engine {

namespace {

bool IsFiniteVec3(const glz::generic& value) {
	if (!value.is_array() || value.size() < 3) {
		return false;
	}
	return value[0u].is_number() && value[1u].is_number() && value[2u].is_number() &&
		   std::isfinite(value[0u].template get<double>()) &&
		   std::isfinite(value[1u].template get<double>()) &&
		   std::isfinite(value[2u].template get<double>());
}

JPH::ShapeSettings::ShapeResult CreateShapeResult(JPH::ShapeSettings& settings) {
	return settings.Create();
}

}  // namespace

ShapeCreateResult PhysicsShapeAssetFactory::CreateShape(const JsonShapeDef& def,
														const MaterialTable& material_table,
														const std::string& assets_dir) {
	ShapeCreateResult result;

	if (def.shapes.has_value() && !def.shapes->empty()) {
		JPH::StaticCompoundShapeSettings compound;
		compound.mSubShapes.reserve(def.shapes->size());

		for (size_t i = 0; i < def.shapes->size(); ++i) {
			const auto& sub_def = (*def.shapes)[i];
			auto sub_result = CreateShape(sub_def, material_table, assets_dir);
			if (!sub_result.shape) {
				return sub_result;
			}

			JPH::RVec3 offset = JPH::RVec3::sZero();
			JPH::Quat rotation = JPH::Quat::sIdentity();
			if (sub_def.position) {
				if (!IsFiniteDoubleVec(*sub_def.position, 3)) {
					result.error = "compound sub-shape position must be [x, y, z]";
					return result;
				}
				offset = ParsePhysicsVec3(*sub_def.position);
			}
			if (sub_def.rotation) {
				if (!IsFiniteFloatVec(*sub_def.rotation, 4)) {
					result.error = "compound sub-shape rotation must be [x, y, z, w]";
					return result;
				}
				rotation = ParsePhysicsQuat(*sub_def.rotation);
			}
			compound.AddShape(JPH::Vec3(offset), rotation, sub_result.shape);
		}

		auto sr = compound.Create();
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
		return result;
	}

	const auto& type = def.type;
	auto& p = const_cast<glz::generic&>(def.params);

	auto get_num = [&](const std::string& key, double default_val = 0.0) -> double {
		if (p.contains(key) && p[key].is_number()) {
			return p[key].template get<double>();
		}
		return default_val;
	};
	auto get_arr = [&](const std::string& key) -> glz::generic& {
		static glz::generic empty;
		if (p.contains(key) && p[key].is_array()) {
			return p[key];
		}
		return empty;
	};

	JPH::RefConst<JPH::PhysicsMaterial> material_ref;
	const JPH::PhysicsMaterial* material = nullptr;
	if (def.material && !def.material->empty()) {
		material_ref = material_table.Get(*def.material);
		material = material_ref.GetPtr();
	}

	if (type == "box") {
		double hx = get_num("halfX", get_num("halfExtent", 0.5));
		double hy = get_num("halfY", get_num("halfExtent", 0.5));
		double hz = get_num("halfZ", get_num("halfExtent", 0.5));
		auto& he_arr = get_arr("halfExtent");
		if (he_arr.is_array() && he_arr.size() >= 3) {
			hx = he_arr[0u].template get<double>();
			hy = he_arr[1u].template get<double>();
			hz = he_arr[2u].template get<double>();
		}
		if (!IsPositiveFinite(hx) || !IsPositiveFinite(hy) || !IsPositiveFinite(hz)) {
			result.error = "box half extents must be finite and > 0";
			return result;
		}
		JPH::BoxShapeSettings settings(
			JPH::Vec3(static_cast<float>(hx), static_cast<float>(hy), static_cast<float>(hz)),
			JPH::cDefaultConvexRadius,
			material);
		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "sphere") {
		double radius = get_num("radius", 0.5);
		if (!IsPositiveFinite(radius)) {
			result.error = "sphere radius must be finite and > 0";
			return result;
		}
		JPH::SphereShapeSettings settings(static_cast<float>(radius), material);
		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "capsule") {
		double half_height = get_num("halfHeight", 0.5);
		double radius = get_num("radius", 0.25);
		if (!IsPositiveFinite(half_height) || !IsPositiveFinite(radius)) {
			result.error = "capsule halfHeight and radius must be finite and > 0";
			return result;
		}
		JPH::CapsuleShapeSettings settings(
			static_cast<float>(half_height), static_cast<float>(radius), material);
		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "cylinder") {
		double half_height = get_num("halfHeight", 0.5);
		double radius = get_num("radius", 0.25);
		if (!IsPositiveFinite(half_height) || !IsPositiveFinite(radius)) {
			result.error = "cylinder halfHeight and radius must be finite and > 0";
			return result;
		}
		JPH::CylinderShapeSettings settings(
			static_cast<float>(half_height), static_cast<float>(radius), JPH::cDefaultConvexRadius, material);
		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "convex_hull" || type == "convexHull") {
		JPH::Array<JPH::Vec3> points;
		auto& pts_arr = get_arr("points");
		if (pts_arr.is_array()) {
			for (size_t i = 0; i < pts_arr.size(); ++i) {
				auto& pt = pts_arr[unsigned(i)];
				if (!IsFiniteVec3(pt)) {
					result.error = "convex_hull point must be [x, y, z] finite numbers";
					return result;
				}
				points.emplace_back(static_cast<float>(pt[0u].template get<double>()),
									static_cast<float>(pt[1u].template get<double>()),
									static_cast<float>(pt[2u].template get<double>()));
			}
		}
		if (points.empty()) {
			result.error = "convex_hull requires non-empty 'points' array";
			return result;
		}
		JPH::ConvexHullShapeSettings settings(points, JPH::cDefaultConvexRadius, material);
		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "mesh" || type == "mesh_shape") {
		JPH::VertexList vertices;
		JPH::IndexedTriangleList triangles;
		auto& verts_arr = get_arr("vertices");
		if (verts_arr.is_array()) {
			for (size_t i = 0; i < verts_arr.size(); ++i) {
				auto& v = verts_arr[unsigned(i)];
				if (!IsFiniteVec3(v)) {
					result.error = "mesh vertex must be [x, y, z] finite numbers";
					return result;
				}
				vertices.emplace_back(static_cast<float>(v[0u].template get<double>()),
									  static_cast<float>(v[1u].template get<double>()),
									  static_cast<float>(v[2u].template get<double>()));
			}
		}
		auto& tris_arr = get_arr("triangles");
		if (tris_arr.is_array()) {
			for (size_t i = 0; i < tris_arr.size(); ++i) {
				auto& t = tris_arr[unsigned(i)];
				if (!t.is_array() || t.size() < 3 || !t[0u].is_number() || !t[1u].is_number() ||
					!t[2u].is_number()) {
					result.error = "mesh triangle must be [i0, i1, i2] numbers";
					return result;
				}
				uint32_t i0 = static_cast<uint32_t>(t[0u].template get<double>());
				uint32_t i1 = static_cast<uint32_t>(t[1u].template get<double>());
				uint32_t i2 = static_cast<uint32_t>(t[2u].template get<double>());
				if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) {
					result.error = "mesh triangle index out of range";
					return result;
				}
				triangles.emplace_back(i0, i1, i2);
			}
		}
		if (vertices.empty() || triangles.empty()) {
			result.error = "mesh requires 'vertices' and 'triangles' arrays";
			return result;
		}

		JPH::MeshShapeSettings settings(vertices, triangles);
		auto& mats_arr = get_arr("materials");
		if (mats_arr.is_array()) {
			std::vector<std::string> mat_names;
			for (size_t i = 0; i < mats_arr.size(); ++i) {
				if (!mats_arr[unsigned(i)].is_string()) {
					result.error = "mesh materials entries must be strings";
					return result;
				}
				mat_names.push_back(mats_arr[unsigned(i)].template get<std::string>());
			}
			settings.mMaterials = material_table.CreateList(mat_names);
		}

		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "height_field" || type == "heightField") {
		JPH::HeightFieldShapeSettings settings;
		bool has_height_samples = false;
		std::string data_path;
		if (p.contains("dataFile") && p["dataFile"].is_string()) {
			data_path = p["dataFile"].template get<std::string>();
		}
		if (!data_path.empty()) {
			data_path = ResolveAssetPath(assets_dir, data_path);
			std::ifstream bf(data_path, std::ios::binary);
			if (!bf) {
				result.error = "height_field: cannot open data file: " + data_path;
				return result;
			}
			bf.seekg(0, std::ios::end);
			size_t file_size = static_cast<size_t>(bf.tellg());
			bf.seekg(0, std::ios::beg);
			size_t sample_count_file = file_size / sizeof(float);
			if (sample_count_file == 0 || sample_count_file > 65536) {
				result.error = "height_field: invalid data file size (0 or >65536 samples)";
				return result;
			}
			uint32_t sample_count = static_cast<uint32_t>(get_num("sampleCount", 0));
			if (sample_count == 0) {
				sample_count = static_cast<uint32_t>(std::sqrt(sample_count_file));
				if (sample_count * sample_count != sample_count_file) {
					result.error =
						"height_field: sampleCount not specified and file size is not a perfect square";
					return result;
				}
			}
			if (static_cast<size_t>(sample_count) * sample_count != sample_count_file) {
				result.error = "height_field: sampleCount does not match data file sample count";
				return result;
			}
			JPH::Array<float> samples;
			samples.resize(sample_count_file);
			bf.read(reinterpret_cast<char*>(samples.data()),
					static_cast<std::streamsize>(sample_count_file * sizeof(float)));
			if (bf.fail()) {
				result.error = "height_field: failed to read data file: " + data_path;
				return result;
			}
			settings.mHeightSamples = std::move(samples);
			settings.mSampleCount = sample_count;
			has_height_samples = true;
		}

		auto& samples_arr = get_arr("samples");
		if (samples_arr.is_array()) {
			JPH::Array<float> samples;
			for (size_t i = 0; i < samples_arr.size(); ++i) {
				if (!samples_arr[unsigned(i)].is_number()) {
					result.error = "height_field samples entries must be numbers";
					return result;
				}
				double sample = samples_arr[unsigned(i)].template get<double>();
				if (!std::isfinite(sample)) {
					result.error = "height_field samples entries must be finite";
					return result;
				}
				samples.push_back(static_cast<float>(sample));
			}
			uint32_t sample_count = static_cast<uint32_t>(get_num("sampleCount", 0));
			if (sample_count == 0) {
				sample_count = static_cast<uint32_t>(std::sqrt(samples.size()));
				if (static_cast<size_t>(sample_count) * sample_count != samples.size()) {
					result.error =
						"height_field: inline sampleCount not specified and sample array size is not a perfect square";
					return result;
				}
			}
			if (sample_count == 0 || sample_count > 65536) {
				result.error = "height_field sampleCount must be in [1, 65536]";
				return result;
			}
			settings.mHeightSamples = std::move(samples);
			settings.mSampleCount = sample_count;
			has_height_samples = true;
		}
		if (!has_height_samples) {
			result.error = "height_field requires dataFile or samples";
			return result;
		}

		auto& off_arr = get_arr("offset");
		if (off_arr.is_array() && off_arr.size() >= 3) {
			if (!IsFiniteVec3(off_arr)) {
				result.error = "height_field offset must be [x, y, z] finite numbers";
				return result;
			}
			settings.mOffset = JPH::Vec3(static_cast<float>(off_arr[0u].template get<double>()),
										 static_cast<float>(off_arr[1u].template get<double>()),
										 static_cast<float>(off_arr[2u].template get<double>()));
		}
		auto& scale_arr = get_arr("scale");
		if (scale_arr.is_array() && scale_arr.size() >= 3) {
			if (!IsFiniteVec3(scale_arr)) {
				result.error = "height_field scale must be [x, y, z] finite numbers";
				return result;
			}
			settings.mScale = JPH::Vec3(static_cast<float>(scale_arr[0u].template get<double>()),
										static_cast<float>(scale_arr[1u].template get<double>()),
										static_cast<float>(scale_arr[2u].template get<double>()));
		}
		auto& mats_arr = get_arr("materials");
		if (mats_arr.is_array()) {
			std::vector<std::string> mat_names;
			for (size_t i = 0; i < mats_arr.size(); ++i) {
				if (!mats_arr[unsigned(i)].is_string()) {
					result.error = "height_field materials entries must be strings";
					return result;
				}
				mat_names.push_back(mats_arr[unsigned(i)].template get<std::string>());
			}
			settings.mMaterials = material_table.CreateList(mat_names);
		}

		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else if (type == "plane") {
		JPH::Plane plane(JPH::Vec3::sAxisY(), 0.0f);
		auto& n_arr = get_arr("normal");
		if (n_arr.is_array() && n_arr.size() >= 3) {
			if (!IsFiniteVec3(n_arr)) {
				result.error = "plane normal must be [x, y, z] finite numbers";
				return result;
			}
			plane = JPH::Plane(JPH::Vec3(static_cast<float>(n_arr[0u].template get<double>()),
										 static_cast<float>(n_arr[1u].template get<double>()),
										 static_cast<float>(n_arr[2u].template get<double>())),
							   static_cast<float>(get_num("constant", 0.0)));
		}
		JPH::PlaneShapeSettings settings(plane, material);
		auto sr = CreateShapeResult(settings);
		if (sr.IsValid()) {
			result.shape = sr.Get();
		} else {
			result.error = sr.GetError();
		}
	} else {
		result.error = "unknown shape type: '" + type +
					   "'. Supported: box, sphere, capsule, cylinder, convex_hull, mesh, height_field, plane, compound";
	}

	return result;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
