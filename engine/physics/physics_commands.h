#pragma once

#ifdef ENGINE_PHYSICS_ENABLED

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>

namespace engine {

//============================================================================
// Command types — exactly 5, per design doc §3.2 [D3]
//============================================================================

enum class CommandType : uint8_t {
    Spawn,        // Spawn(protoId, transform)
    Destroy,      // Destroy(objectId)
    ApplyForce,   // ApplyForce(objectId, force, point)
    SetVelocity,  // SetVelocity(objectId, velocity)
    Tick,         // Tick(frameId, fixedDeltaTime)
};

//============================================================================
// Command argument structs
//============================================================================

struct SpawnArgs {
    std::string proto_id;
    JPH::RVec3 position = JPH::RVec3::sZero();
    JPH::Quat rotation = JPH::Quat::sIdentity();
    uint64_t user_data = 0;
};

struct DestroyArgs {
    uint32_t body_id = 0;
};

struct ApplyForceArgs {
    uint32_t body_id = 0;
    JPH::Vec3 force = JPH::Vec3::sZero();
    JPH::RVec3 point = JPH::RVec3::sZero();  // world-space
};

struct SetVelocityArgs {
    uint32_t body_id = 0;
    JPH::Vec3 velocity = JPH::Vec3::sZero();  // linear velocity
};

struct TickArgs {
    uint64_t frame_id = 0;
    float delta_time = 0.0f;
};

//============================================================================
// PhysicsCommand — tagged union over 5 command types
//============================================================================

struct PhysicsCommand {
    CommandType type;
    std::variant<std::monostate, SpawnArgs, DestroyArgs, ApplyForceArgs,
                 SetVelocityArgs, TickArgs> args;

    // Convenience constructors
    static PhysicsCommand MakeSpawn(SpawnArgs a) {
        return {CommandType::Spawn, std::move(a)};
    }
    static PhysicsCommand MakeDestroy(DestroyArgs a) {
        return {CommandType::Destroy, std::move(a)};
    }
    static PhysicsCommand MakeApplyForce(ApplyForceArgs a) {
        return {CommandType::ApplyForce, std::move(a)};
    }
    static PhysicsCommand MakeSetVelocity(SetVelocityArgs a) {
        return {CommandType::SetVelocity, std::move(a)};
    }
    static PhysicsCommand MakeTick(TickArgs a) {
        return {CommandType::Tick, std::move(a)};
    }
};

//============================================================================
// Result structures — per design doc §3.3 [D6]
//============================================================================

struct DiffPacket {
    uint32_t object_id = 0;
    // bit0: position, bit1: rotation, bit2: linearVel, bit3: angularVel, bit4-15: reserved
    uint16_t change_mask = 0;
    std::vector<float> values;  // ordered by mask bit index
};

struct CollisionEvent {
    uint32_t body_a = 0;
    uint32_t body_b = 0;

    enum class Type : uint8_t {
        Start,    // contact added
        Persist,  // contact persisted
        End       // contact removed
    };
    Type type = Type::Start;

    std::vector<JPH::RVec3> contact_points;  // world-space contact points
};

struct BodyTransform {
    uint32_t body_id = 0;
    double pos_x = 0.0, pos_y = 0.0, pos_z = 0.0;
    float rot_x = 0.0f, rot_y = 0.0f, rot_z = 0.0f, rot_w = 1.0f;  // quaternion
};

struct PhysicsFrameResult {
    uint64_t frame_id = 0;
    std::vector<BodyTransform> transforms;
    std::vector<CollisionEvent> collision_events;
    std::vector<DiffPacket> diff_packets;
    std::string error;  // empty = ok
};

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
