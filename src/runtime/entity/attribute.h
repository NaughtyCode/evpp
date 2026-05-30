#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

#include "entity_id.h"

namespace engine {
namespace entity {

using AttrValue = std::variant<int64_t, double, std::string, bool>;

using AttrChangeCallback = std::function<void(
	EntityId id, const std::string& key, const AttrValue& old_val, const AttrValue& new_val)>;

class AttributeTable {
public:
	void SetOwnerId(EntityId id) { owner_id_ = id; }

	void SetChangeCallback(AttrChangeCallback cb) { on_change_ = std::move(cb); }

	void Set(const std::string& key, AttrValue value) {
		auto it = attrs_.find(key);
		if (it != attrs_.end()) {
			if (it->second == value) {
				return;
			}
			AttrValue old = it->second;
			it->second = std::move(value);
			if (on_change_) {
				on_change_(owner_id_, key, old, it->second);
			}
		} else {
			attrs_.emplace(key, std::move(value));
		}
	}

	const AttrValue* TryGet(const std::string& key) const {
		auto it = attrs_.find(key);
		return it != attrs_.end() ? &it->second : nullptr;
	}

	AttrValue Get(const std::string& key, const AttrValue& default_val = {}) const {
		auto it = attrs_.find(key);
		return it != attrs_.end() ? it->second : default_val;
	}

	bool Has(const std::string& key) const {
		return attrs_.find(key) != attrs_.end();
	}

	void Remove(const std::string& key) {
		attrs_.erase(key);
	}

	void Clear() { attrs_.clear(); }

	size_t Count() const { return attrs_.size(); }

private:
	EntityId owner_id_ = kInvalidEntityId;
	std::unordered_map<std::string, AttrValue> attrs_;
	AttrChangeCallback on_change_;
};

}  // namespace entity
}  // namespace engine
