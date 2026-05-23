#ifdef ENGINE_PHYSICS_ENABLED

#include "engine/physics/physics_materials.h"

#include <vector>

#include <glaze/glaze.hpp>

JPH_NAMESPACE_BEGIN

// Register PhysicsMaterialSimple with Jolt's RTTI factory.
// This is required for serialization support.
JPH_IMPLEMENT_SERIALIZABLE_VIRTUAL(engine::PhysicsMaterialSimple)
{
    JPH_ADD_BASE_CLASS(engine::PhysicsMaterialSimple, JPH::PhysicsMaterial)
    JPH_ADD_ATTRIBUTE(engine::PhysicsMaterialSimple, name_)
    JPH_ADD_ATTRIBUTE(engine::PhysicsMaterialSimple, friction_)
    JPH_ADD_ATTRIBUTE(engine::PhysicsMaterialSimple, restitution_)
}

JPH_NAMESPACE_END

namespace engine {

//============================================================================
// PhysicsMaterialSimple serialization
//============================================================================

void PhysicsMaterialSimple::SaveBinaryState(JPH::StreamOut& inStream) const {
    JPH::PhysicsMaterial::SaveBinaryState(inStream);
    inStream.Write(name_);
    inStream.Write(friction_);
    inStream.Write(restitution_);
}

void PhysicsMaterialSimple::RestoreBinaryState(JPH::StreamIn& inStream) {
    JPH::PhysicsMaterial::RestoreBinaryState(inStream);
    inStream.Read(name_);
    inStream.Read(friction_);
    inStream.Read(restitution_);
}

} // namespace engine

//============================================================================
// glaze reflection for MaterialEntry
//============================================================================

template <>
struct glz::meta<engine::MaterialEntry> {
    using T = engine::MaterialEntry;
    static constexpr auto value = glz::object(
        "name", &T::name,
        "friction", &T::friction,
        "restitution", &T::restitution
    );
};

namespace engine {

//============================================================================
// MaterialTable
//============================================================================

bool MaterialTable::LoadFromJson(const std::string& json) {
    std::vector<MaterialEntry> entries;
    auto ec = glz::read_json(entries, json);
    if (ec) {
        return false;
    }
    Register(entries);
    return true;
}

void MaterialTable::Register(const std::vector<MaterialEntry>& entries) {
    for (const auto& entry : entries) {
        auto mat = JPH::Ref<PhysicsMaterialSimple>(
            new PhysicsMaterialSimple(entry.name, entry.friction, entry.restitution));
        JPH::PhysicsMaterial::RefConst mat_ref = mat;
        materials_[entry.name] = mat_ref;
        owned_[entry.name] = std::move(mat);  // hold ownership
    }
}

JPH::PhysicsMaterial::RefConst MaterialTable::Get(const std::string& name) const {
    auto it = materials_.find(name);
    if (it != materials_.end()) {
        return it->second;
    }
    return JPH::PhysicsMaterial::sDefault;
}

std::vector<std::string> MaterialTable::GetNames() const {
    std::vector<std::string> names;
    names.reserve(materials_.size());
    for (const auto& [name, _] : materials_) {
        names.push_back(name);
    }
    return names;
}

JPH::PhysicsMaterialList MaterialTable::CreateList(
    const std::vector<std::string>& names) const {
    JPH::PhysicsMaterialList list;
    list.reserve(names.size());
    for (const auto& name : names) {
        list.push_back(Get(name));
    }
    return list;
}

void MaterialTable::Clear() {
    materials_.clear();
    owned_.clear();
}

} // namespace engine

#endif // ENGINE_PHYSICS_ENABLED
