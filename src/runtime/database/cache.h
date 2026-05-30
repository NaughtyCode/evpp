#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "runtime/core/engine_api.h"

namespace engine {
namespace database {

// LRU cache for database entities.
// Thread-safe for concurrent reads; writes lock the full cache.
template <typename T>
class EntityCache {
public:
	explicit EntityCache(size_t max_entries = 10000)
		: max_entries_(max_entries) {}

	// Look up an entity by key. Returns nullopt on miss.
	std::optional<T> Get(const std::string& key) {
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = index_.find(key);
		if (it == index_.end()) {
			++miss_count_;
			return std::nullopt;
		}

		++hit_count_;
		// Move to front of LRU list
		lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
		return it->second->second;
	}

	// Insert or update an entity.
	void Put(const std::string& key, const T& value) {
		std::lock_guard<std::mutex> lock(mutex_);
		if (max_entries_ == 0) return;

		auto it = index_.find(key);
		if (it != index_.end()) {
			it->second->second = value;
			lru_list_.splice(lru_list_.begin(), lru_list_, it->second);
			return;
		}

		// Evict oldest if at capacity
		while (!lru_list_.empty() && lru_list_.size() >= max_entries_) {
			const auto& back = lru_list_.back();
			index_.erase(back.first);
			lru_list_.pop_back();
			++evict_count_;
		}

		lru_list_.emplace_front(key, value);
		index_[key] = lru_list_.begin();
	}

	// Remove an entity from the cache.
	void Invalidate(const std::string& key) {
		std::lock_guard<std::mutex> lock(mutex_);
		auto it = index_.find(key);
		if (it == index_.end()) return;
		lru_list_.erase(it->second);
		index_.erase(it);
	}

	// Clear all cached entries.
	void Clear() {
		std::lock_guard<std::mutex> lock(mutex_);
		lru_list_.clear();
		index_.clear();
	}

	// Statistics
	size_t HitCount() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return hit_count_;
	}
	size_t MissCount() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return miss_count_;
	}
	size_t EvictCount() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return evict_count_;
	}
	size_t Capacity() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return max_entries_;
	}
	size_t Size() const {
		std::lock_guard<std::mutex> lock(mutex_);
		return lru_list_.size();
	}
	double HitRate() const {
		std::lock_guard<std::mutex> lock(mutex_);
		size_t total = hit_count_ + miss_count_;
		return total > 0 ? static_cast<double>(hit_count_) / static_cast<double>(total) : 0.0;
	}
	void SetCapacity(size_t max_entries) {
		std::lock_guard<std::mutex> lock(mutex_);
		max_entries_ = max_entries;
		while (lru_list_.size() > max_entries_) {
			const auto& back = lru_list_.back();
			index_.erase(back.first);
			lru_list_.pop_back();
			++evict_count_;
		}
	}
	void ResetStats() {
		std::lock_guard<std::mutex> lock(mutex_);
		hit_count_ = 0;
		miss_count_ = 0;
		evict_count_ = 0;
	}

private:
	size_t max_entries_;
	std::list<std::pair<std::string, T>> lru_list_;
	std::unordered_map<std::string, decltype(lru_list_.begin())> index_;

	size_t hit_count_ = 0;
	size_t miss_count_ = 0;
	size_t evict_count_ = 0;
	mutable std::mutex mutex_;
};

}  // namespace database
}  // namespace engine
