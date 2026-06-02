#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_asset_common.h"

#include <cmath>

namespace engine {

JPH::RVec3 ParsePhysicsVec3(const std::vector<double>& v) {
	return JPH::RVec3(static_cast<JPH::Real>(v.size() > 0 ? v[0] : 0.0),
					  static_cast<JPH::Real>(v.size() > 1 ? v[1] : 0.0),
					  static_cast<JPH::Real>(v.size() > 2 ? v[2] : 0.0));
}

JPH::Vec3 ParsePhysicsFloatVec3(const std::vector<float>& v) {
	return JPH::Vec3(v.size() > 0 ? v[0] : 0.0f,
					 v.size() > 1 ? v[1] : 0.0f,
					 v.size() > 2 ? v[2] : 0.0f);
}

JPH::Quat ParsePhysicsQuat(const std::vector<float>& q) {
	if (q.size() >= 4) {
		return NormalizePhysicsQuat(JPH::Quat(q[0], q[1], q[2], q[3]));
	}
	return JPH::Quat::sIdentity();
}

JPH::Quat NormalizePhysicsQuat(JPH::QuatArg q) {
	return q.LengthSq() > 1.0e-12f ? q.Normalized() : JPH::Quat::sIdentity();
}

bool IsPositiveFinite(double value) {
	return std::isfinite(value) && value > 0.0;
}

bool IsFiniteFloat(float value) {
	return std::isfinite(value);
}

bool IsFiniteDoubleVec(const std::vector<double>& values, size_t expected) {
	if (values.size() != expected) {
		return false;
	}
	for (double value : values) {
		if (!std::isfinite(value)) {
			return false;
		}
	}
	return true;
}

bool IsFiniteFloatVec(const std::vector<float>& values, size_t expected) {
	if (values.size() != expected) {
		return false;
	}
	for (float value : values) {
		if (!std::isfinite(value)) {
			return false;
		}
	}
	return true;
}

bool IsMaterialValid(const JsonMaterial& material) {
	return std::isfinite(material.friction) && material.friction >= 0.0f &&
		   std::isfinite(material.restitution) && material.restitution >= 0.0f;
}

JPH::EMotionType ParseMotionType(const std::string& s) {
	if (s == "static") return JPH::EMotionType::Static;
	if (s == "kinematic") return JPH::EMotionType::Kinematic;
	return JPH::EMotionType::Dynamic;
}

bool IsMotionTypeName(const std::string& s) {
	return s == "static" || s == "kinematic" || s == "dynamic";
}

JPH::EMotionQuality ParseMotionQuality(const std::string& s) {
	if (s == "linear_cast" || s == "linearCast") return JPH::EMotionQuality::LinearCast;
	return JPH::EMotionQuality::Discrete;
}

bool IsMotionQualityName(const std::string& s) {
	return s == "linear_cast" || s == "linearCast" || s == "discrete";
}

bool BuildAllowedDofs(const std::vector<uint8_t>& dofs, uint8_t& out_mask, std::string& out_error) {
	out_mask = 0;
	for (uint8_t dof : dofs) {
		if (dof >= 6) {
			out_error = "allowedDofs entries must be in [0, 5]";
			return false;
		}
		out_mask |= static_cast<uint8_t>(1u << dof);
	}
	if (out_mask == 0) {
		out_error = "allowedDofs has no valid axes";
		return false;
	}
	return true;
}

bool ResolveObjectLayer(const LayerConfig& layer_config,
						const std::string& name,
						JPH::ObjectLayer& out_layer,
						std::string& out_error) {
	auto layer_it = layer_config.object_layers.find(name);
	if (layer_it == layer_config.object_layers.end()) {
		out_error = "unknown objectLayer '" + name + "'";
		return false;
	}
	out_layer = JPH::ObjectLayer(layer_it->second);
	return true;
}

std::string GetAssetDirectory(const std::string& asset_path) {
	size_t slash = asset_path.rfind('/');
	if (slash == std::string::npos) slash = asset_path.rfind('\\');
	if (slash == std::string::npos) {
		return {};
	}
	return asset_path.substr(0, slash);
}

std::string ResolveAssetPath(const std::string& assets_dir, const std::string& path) {
	if (path.empty() || assets_dir.empty() || path[0] == '/' ||
		(path.size() >= 2 && path[1] == ':')) {
		return path;
	}
	return assets_dir + "/" + path;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
