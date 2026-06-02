#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_shape_assets.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
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
	if (!value.is_array() || value.size() != 3) {
		return false;
	}
	constexpr double kMaxFloat = static_cast<double>(std::numeric_limits<float>::max());
	return value[0u].is_number() && value[1u].is_number() && value[2u].is_number() &&
		   std::isfinite(value[0u].template get<double>()) &&
		   std::isfinite(value[1u].template get<double>()) &&
		   std::isfinite(value[2u].template get<double>()) &&
		   std::abs(value[0u].template get<double>()) <= kMaxFloat &&
		   std::abs(value[1u].template get<double>()) <= kMaxFloat &&
		   std::abs(value[2u].template get<double>()) <= kMaxFloat;
}

bool ReadUint32(const glz::generic& value, uint32_t& out_value) {
	if (!value.is_number()) {
		return false;
	}
	double number = value.template get<double>();
	if (!std::isfinite(number) || number < 0.0 ||
		number > static_cast<double>(std::numeric_limits<uint32_t>::max()) ||
		std::floor(number) != number) {
		return false;
	}
	out_value = static_cast<uint32_t>(number);
	return true;
}

bool ReadSampleCount(glz::generic& params,
					 uint32_t& out_sample_count,
					 bool& out_has_sample_count,
					 std::string& out_error) {
	out_sample_count = 0;
	out_has_sample_count = false;
	if (!params.contains("sampleCount")) {
		return true;
	}
	out_has_sample_count = true;
	if (!ReadUint32(params["sampleCount"], out_sample_count) || out_sample_count == 0 ||
		out_sample_count > 65536) {
		out_error = "height_field sampleCount must be an integer in [1, 65536]";
		return false;
	}
	return true;
}

bool InferSquareSampleCount(size_t total_samples, uint32_t& out_sample_count) {
	if (total_samples == 0) {
		return false;
	}
	uint32_t sample_count = static_cast<uint32_t>(std::sqrt(static_cast<double>(total_samples)));
	if (static_cast<size_t>(sample_count) * sample_count != total_samples) {
		return false;
	}
	out_sample_count = sample_count;
	return true;
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

	auto read_num = [&](const std::string& key,
						double default_val,
						double& out_value,
						bool allow_array = false) -> bool {
		if (!p.contains(key)) {
			out_value = default_val;
			return true;
		}
		if (allow_array && p[key].is_array()) {
			out_value = default_val;
			return true;
		}
		if (!p[key].is_number()) {
			result.error = key + " must be a finite number";
			return false;
		}
		double value = p[key].template get<double>();
		if (!std::isfinite(value) ||
			std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
			result.error = key + " must be a finite float-range number";
			return false;
		}
		out_value = value;
		return true;
	};
	auto read_arr = [&](const std::string& key, glz::generic*& out_value) -> bool {
		out_value = nullptr;
		if (!p.contains(key)) {
			return true;
		}
		if (!p[key].is_array()) {
			result.error = key + " must be an array";
			return false;
		}
		out_value = &p[key];
		return true;
	};

	JPH::RefConst<JPH::PhysicsMaterial> material_ref;
	const JPH::PhysicsMaterial* material = nullptr;
	if (def.material && !def.material->empty()) {
		if (!material_table.Has(*def.material)) {
			result.error = "unknown material: " + *def.material;
			return result;
		}
		material_ref = material_table.Get(*def.material);
		material = material_ref.GetPtr();
	}

	if (type == "box") {
		double half_extent = 0.5;
		if (!read_num("halfExtent", 0.5, half_extent, true)) {
			return result;
		}
		double hx = half_extent;
		double hy = half_extent;
		double hz = half_extent;
		if (!read_num("halfX", half_extent, hx) || !read_num("halfY", half_extent, hy) ||
			!read_num("halfZ", half_extent, hz)) {
			return result;
		}
		glz::generic* he_arr = nullptr;
		if (!read_arr("halfExtent", he_arr)) {
			return result;
		}
		if (he_arr) {
			if (!IsFiniteVec3(*he_arr)) {
				result.error = "box halfExtent must be [x, y, z] finite numbers";
				return result;
			}
			hx = (*he_arr)[0u].template get<double>();
			hy = (*he_arr)[1u].template get<double>();
			hz = (*he_arr)[2u].template get<double>();
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
		double radius = 0.5;
		if (!read_num("radius", 0.5, radius)) {
			return result;
		}
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
		double half_height = 0.5;
		double radius = 0.25;
		if (!read_num("halfHeight", 0.5, half_height) || !read_num("radius", 0.25, radius)) {
			return result;
		}
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
		double half_height = 0.5;
		double radius = 0.25;
		if (!read_num("halfHeight", 0.5, half_height) || !read_num("radius", 0.25, radius)) {
			return result;
		}
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
		glz::generic* pts_arr = nullptr;
		if (!read_arr("points", pts_arr)) {
			return result;
		}
		if (pts_arr) {
			for (size_t i = 0; i < pts_arr->size(); ++i) {
				auto& pt = (*pts_arr)[unsigned(i)];
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
		glz::generic* verts_arr = nullptr;
		if (!read_arr("vertices", verts_arr)) {
			return result;
		}
		if (verts_arr) {
			for (size_t i = 0; i < verts_arr->size(); ++i) {
				auto& v = (*verts_arr)[unsigned(i)];
				if (!IsFiniteVec3(v)) {
					result.error = "mesh vertex must be [x, y, z] finite numbers";
					return result;
				}
				vertices.emplace_back(static_cast<float>(v[0u].template get<double>()),
									  static_cast<float>(v[1u].template get<double>()),
									  static_cast<float>(v[2u].template get<double>()));
			}
		}
		glz::generic* tris_arr = nullptr;
		if (!read_arr("triangles", tris_arr)) {
			return result;
		}
		if (tris_arr) {
			for (size_t i = 0; i < tris_arr->size(); ++i) {
				auto& t = (*tris_arr)[unsigned(i)];
				uint32_t i0 = 0;
				uint32_t i1 = 0;
				uint32_t i2 = 0;
				if (!t.is_array() || t.size() != 3 || !ReadUint32(t[0u], i0) ||
					!ReadUint32(t[1u], i1) || !ReadUint32(t[2u], i2)) {
					result.error = "mesh triangle must be [i0, i1, i2] non-negative integer indices";
					return result;
				}
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
		glz::generic* mats_arr = nullptr;
		if (!read_arr("materials", mats_arr)) {
			return result;
		}
		if (mats_arr) {
			std::vector<std::string> mat_names;
			for (size_t i = 0; i < mats_arr->size(); ++i) {
				if (!(*mats_arr)[unsigned(i)].is_string()) {
					result.error = "mesh materials entries must be strings";
					return result;
				}
				std::string name = (*mats_arr)[unsigned(i)].template get<std::string>();
				if (!material_table.Has(name)) {
					result.error = "mesh material not found: " + name;
					return result;
				}
				mat_names.push_back(std::move(name));
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
		if (p.contains("dataFile") && !p["dataFile"].is_string()) {
			result.error = "height_field dataFile must be a string";
			return result;
		}
		if (p.contains("dataFile") && p["dataFile"].is_string()) {
			data_path = p["dataFile"].template get<std::string>();
		}
		glz::generic* samples_arr = nullptr;
		if (!read_arr("samples", samples_arr)) {
			return result;
		}
		if (!data_path.empty() && samples_arr) {
			result.error = "height_field requires either dataFile or samples, not both";
			return result;
		}
		if (!data_path.empty()) {
			data_path = ResolveAssetPath(assets_dir, data_path);
			std::ifstream bf(data_path, std::ios::binary);
			if (!bf) {
				result.error = "height_field: cannot open data file: " + data_path;
				return result;
			}
			bf.seekg(0, std::ios::end);
			std::streampos end = bf.tellg();
			if (end <= 0) {
				result.error = "height_field: invalid data file size (0 bytes): " + data_path;
				return result;
			}
			size_t file_size = static_cast<size_t>(end);
			bf.seekg(0, std::ios::beg);
			if (file_size % sizeof(float) != 0) {
				result.error = "height_field: data file size must be a multiple of float size";
				return result;
			}
			size_t sample_count_file = file_size / sizeof(float);
			if (sample_count_file == 0 || sample_count_file > 65536) {
				result.error = "height_field: invalid data file size (0 or >65536 samples)";
				return result;
			}
			uint32_t sample_count = 0;
			bool has_sample_count = false;
			if (!ReadSampleCount(p, sample_count, has_sample_count, result.error)) {
				return result;
			}
			if (!has_sample_count) {
				if (!InferSquareSampleCount(sample_count_file, sample_count)) {
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
			for (float sample : samples) {
				if (!std::isfinite(sample)) {
					result.error = "height_field: data file contains non-finite samples: " + data_path;
					return result;
				}
			}
			settings.mHeightSamples = std::move(samples);
			settings.mSampleCount = sample_count;
			has_height_samples = true;
		}

		if (samples_arr) {
			JPH::Array<float> samples;
			for (size_t i = 0; i < samples_arr->size(); ++i) {
				if (!(*samples_arr)[unsigned(i)].is_number()) {
					result.error = "height_field samples entries must be numbers";
					return result;
				}
				double sample = (*samples_arr)[unsigned(i)].template get<double>();
				if (!std::isfinite(sample) ||
					std::abs(sample) >
						static_cast<double>(std::numeric_limits<float>::max())) {
					result.error = "height_field samples entries must be finite float-range numbers";
					return result;
				}
				samples.push_back(static_cast<float>(sample));
			}
			if (samples.empty() || samples.size() > 65536) {
				result.error = "height_field samples size must be in [1, 65536]";
				return result;
			}
			uint32_t sample_count = 0;
			bool has_sample_count = false;
			if (!ReadSampleCount(p, sample_count, has_sample_count, result.error)) {
				return result;
			}
			if (!has_sample_count) {
				if (!InferSquareSampleCount(samples.size(), sample_count)) {
					result.error =
						"height_field: inline sampleCount not specified and sample array size is not a perfect square";
					return result;
				}
			}
			if (static_cast<size_t>(sample_count) * sample_count != samples.size()) {
				result.error = "height_field: sampleCount does not match inline sample count";
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

		glz::generic* off_arr = nullptr;
		if (!read_arr("offset", off_arr)) {
			return result;
		}
		if (off_arr) {
			if (!IsFiniteVec3(*off_arr)) {
				result.error = "height_field offset must be [x, y, z] finite numbers";
				return result;
			}
			settings.mOffset = JPH::Vec3(static_cast<float>((*off_arr)[0u].template get<double>()),
										 static_cast<float>((*off_arr)[1u].template get<double>()),
										 static_cast<float>((*off_arr)[2u].template get<double>()));
		}
		glz::generic* scale_arr = nullptr;
		if (!read_arr("scale", scale_arr)) {
			return result;
		}
		if (scale_arr) {
			if (!IsFiniteVec3(*scale_arr)) {
				result.error = "height_field scale must be [x, y, z] finite numbers";
				return result;
			}
			double sx = (*scale_arr)[0u].template get<double>();
			double sy = (*scale_arr)[1u].template get<double>();
			double sz = (*scale_arr)[2u].template get<double>();
			if (!IsPositiveFinite(sx) || !IsPositiveFinite(sy) || !IsPositiveFinite(sz)) {
				result.error = "height_field scale components must be finite and > 0";
				return result;
			}
			settings.mScale =
				JPH::Vec3(static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz));
		}
		glz::generic* height_mats_arr = nullptr;
		if (!read_arr("materials", height_mats_arr)) {
			return result;
		}
		if (height_mats_arr) {
			std::vector<std::string> mat_names;
			for (size_t i = 0; i < height_mats_arr->size(); ++i) {
				if (!(*height_mats_arr)[unsigned(i)].is_string()) {
					result.error = "height_field materials entries must be strings";
					return result;
				}
				std::string name = (*height_mats_arr)[unsigned(i)].template get<std::string>();
				if (!material_table.Has(name)) {
					result.error = "height_field material not found: " + name;
					return result;
				}
				mat_names.push_back(std::move(name));
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
		glz::generic* n_arr = nullptr;
		if (!read_arr("normal", n_arr)) {
			return result;
		}
		if (n_arr) {
			if (!IsFiniteVec3(*n_arr)) {
				result.error = "plane normal must be [x, y, z] finite numbers";
				return result;
			}
			JPH::Vec3 normal(static_cast<float>((*n_arr)[0u].template get<double>()),
							 static_cast<float>((*n_arr)[1u].template get<double>()),
							 static_cast<float>((*n_arr)[2u].template get<double>()));
			if (normal.LengthSq() <= 1.0e-12f) {
				result.error = "plane normal must be non-zero";
				return result;
			}
			double constant = 0.0;
			if (!read_num("constant", 0.0, constant)) {
				return result;
			}
			plane = JPH::Plane(normal.Normalized(), static_cast<float>(constant));
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
