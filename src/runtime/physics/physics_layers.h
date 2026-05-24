#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <string>
#include <unordered_map>

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

#include "runtime/physics/physics_config.h"

namespace engine {

//============================================================================
// BPLayerInterfaceImpl — maps ObjectLayer → BroadPhaseLayer [J7]
//============================================================================

class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    explicit BPLayerInterfaceImpl(const LayerConfig& config);

    unsigned int GetNumBroadPhaseLayers() const override;
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override;
#endif

private:
    unsigned int num_layers_ = 0;
    // ObjectLayer → BroadPhaseLayer lookup
    std::unordered_map<JPH::ObjectLayer, JPH::BroadPhaseLayer> obj_to_bp_;

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    std::unordered_map<JPH::BroadPhaseLayer::Type, std::string> bp_layer_names_;
#endif
};

//============================================================================
// ObjectLayerPairFilterImpl — checks if two ObjectLayers should collide [J7]
//============================================================================

class ObjectLayerPairFilterImpl final : public JPH::ObjectLayerPairFilter {
public:
    explicit ObjectLayerPairFilterImpl(const LayerConfig& config);

    bool ShouldCollide(JPH::ObjectLayer inLayer1,
                       JPH::ObjectLayer inLayer2) const override;

private:
    // Packed key: (layerA << 16) | layerB
    std::unordered_map<uint32_t, bool> collision_rules_;
    bool default_collide_ = false;
};

//============================================================================
// ObjectVSBLayerFilterImpl — checks if ObjectLayer collides with BroadPhaseLayer
//============================================================================

class ObjectVSBLayerFilterImpl final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    explicit ObjectVSBLayerFilterImpl(const LayerConfig& config,
                                      const BPLayerInterfaceImpl& bp_iface);

    bool ShouldCollide(JPH::ObjectLayer inLayer1,
                       JPH::BroadPhaseLayer inLayer2) const override;

private:
    // ObjectLayer → BroadPhaseLayer → collide
    std::unordered_map<JPH::ObjectLayer,
                       std::unordered_map<JPH::BroadPhaseLayer::Type, bool>> rules_;
    bool default_collide_ = false;
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
