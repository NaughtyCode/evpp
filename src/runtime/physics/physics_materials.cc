#ifdef ENGINE_PHYSICS_ENABLED

#include "runtime/physics/physics_materials.h"

#include <vector>

#include <glaze/glaze.hpp>

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
        materials_[entry.name] = mat;  // Ref<Derived> → RefConst<Derived>
        owned_[entry.name] = std::move(mat);  // hold ownership
    }
}

JPH::RefConst<JPH::PhysicsMaterial> MaterialTable::Get(const std::string& name) const {
    auto it = materials_.find(name);
    if (it != materials_.end()) {
        return JPH::RefConst<JPH::PhysicsMaterial>(it->second.GetPtr());
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
