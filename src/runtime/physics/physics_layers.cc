#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_layers.h"

#include <cassert>

namespace engine {

//============================================================================
// BPLayerInterfaceImpl
//============================================================================

BPLayerInterfaceImpl::BPLayerInterfaceImpl(const LayerConfig& config) {
	// Build ObjectLayer → BroadPhaseLayer mapping from layer_mapping
	for (const auto& [obj_name, bp_name] : config.layer_mapping) {
		auto obj_it = config.object_layers.find(obj_name);
		auto bp_it = config.broad_phase_layers.find(bp_name);
		if (obj_it == config.object_layers.end() || bp_it == config.broad_phase_layers.end()) {
			continue;  // skip invalid entries
		}
		JPH::ObjectLayer obj_layer(obj_it->second);
		JPH::BroadPhaseLayer bp_layer(bp_it->second);
		obj_to_bp_[obj_layer] = bp_layer;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
		bp_layer_names_[bp_layer.GetValue()] = bp_name;
#endif
	}

	// Count unique broad phase layers
	std::unordered_map<JPH::BroadPhaseLayer::Type, bool> unique_bp;
	for (const auto& [obj, bp] : obj_to_bp_) {
		unique_bp[bp.GetValue()] = true;
	}
	num_layers_ = static_cast<unsigned int>(unique_bp.size());
}

unsigned int BPLayerInterfaceImpl::GetNumBroadPhaseLayers() const {
	return num_layers_ > 0 ? num_layers_ : 1;  // at least 1
}

JPH::BroadPhaseLayer BPLayerInterfaceImpl::GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const {
	auto it = obj_to_bp_.find(inLayer);
	if (it != obj_to_bp_.end()) {
		return it->second;
	}
	return JPH::BroadPhaseLayer(0);	 // fallback
}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
const char* BPLayerInterfaceImpl::GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const {
	auto it = bp_layer_names_.find(inLayer.GetValue());
	if (it != bp_layer_names_.end()) {
		return it->second.c_str();
	}
	return "unknown";
}
#endif

//============================================================================
// ObjectLayerPairFilterImpl
//============================================================================

ObjectLayerPairFilterImpl::ObjectLayerPairFilterImpl(const LayerConfig& config) {
	for (const auto& rule : config.collision_matrix) {
		auto it_a = config.object_layers.find(rule.layer_a);
		auto it_b = config.object_layers.find(rule.layer_b);
		if (it_a == config.object_layers.end() || it_b == config.object_layers.end()) {
			continue;  // skip rules referencing unknown layers
		}
		uint16_t a = it_a->second;
		uint16_t b = it_b->second;
		// Store both orderings for lock-free symmetric lookup
		collision_rules_[(static_cast<uint32_t>(a) << 16) | b] = rule.collide;
		collision_rules_[(static_cast<uint32_t>(b) << 16) | a] = rule.collide;
	}
}

bool ObjectLayerPairFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1,
											  JPH::ObjectLayer inLayer2) const {
	uint32_t key = (static_cast<uint32_t>(inLayer1) << 16) | inLayer2;
	auto it = collision_rules_.find(key);
	if (it != collision_rules_.end()) {
		return it->second;
	}
	return default_collide_;
}

//============================================================================
// ObjectVSBLayerFilterImpl
//============================================================================

ObjectVSBLayerFilterImpl::ObjectVSBLayerFilterImpl(const LayerConfig& config,
												   const BPLayerInterfaceImpl& bp_iface) {
	// Build a local collision lookup from config.collision_matrix
	std::unordered_map<uint32_t, bool> collision_rules;
	for (const auto& rule : config.collision_matrix) {
		auto it_a = config.object_layers.find(rule.layer_a);
		auto it_b = config.object_layers.find(rule.layer_b);
		if (it_a == config.object_layers.end() || it_b == config.object_layers.end()) {
			continue;
		}
		uint16_t a = it_a->second;
		uint16_t b = it_b->second;
		collision_rules[(static_cast<uint32_t>(a) << 16) | b] = rule.collide;
		collision_rules[(static_cast<uint32_t>(b) << 16) | a] = rule.collide;
	}

	// Build filter: for each ObjectLayer, determine which BroadPhaseLayers it collides with
	for (const auto& [obj_name, obj_val] : config.object_layers) {
		JPH::ObjectLayer obj_layer(obj_val);
		JPH::BroadPhaseLayer bp_layer = bp_iface.GetBroadPhaseLayer(obj_layer);

		// Check collision rules for this layer against all other layers
		for (const auto& [other_name, other_val] : config.object_layers) {
			JPH::BroadPhaseLayer other_bp =
				bp_iface.GetBroadPhaseLayer(JPH::ObjectLayer(other_val));

			// Look up collision rule between these two object layers
			uint32_t key = (static_cast<uint32_t>(obj_val) << 16) | other_val;
			auto cit = collision_rules.find(key);
			bool collide = default_collide_;
			if (cit != collision_rules.end()) {
				collide = cit->second;
			}

			// An ObjectLayer collides with a BroadPhaseLayer if any object
			// in that BP layer has a colliding rule with this object layer
			if (collide) {
				rules_[obj_layer][other_bp.GetValue()] = true;
			}
		}

		// Ensure the mapping for this object's own BP layer is populated
		if (rules_[obj_layer].find(bp_layer.GetValue()) == rules_[obj_layer].end()) {
			rules_[obj_layer][bp_layer.GetValue()] = false;
		}
	}
}

bool ObjectVSBLayerFilterImpl::ShouldCollide(JPH::ObjectLayer inLayer1,
											 JPH::BroadPhaseLayer inLayer2) const {
	auto it1 = rules_.find(inLayer1);
	if (it1 == rules_.end()) {
		return default_collide_;
	}
	auto it2 = it1->second.find(inLayer2.GetValue());
	if (it2 == it1->second.end()) {
		return default_collide_;
	}
	return it2->second;
}

}  // namespace engine

#endif	// ENGINE_PHYSICS_ENABLED
