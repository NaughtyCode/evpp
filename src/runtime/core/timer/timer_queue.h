// SPDX-License-Identifier: MIT
// Copyright (c) 2024 GameTimerLib
//
// Timer queue — a red-black tree based priority queue for timer nodes,
// ordered by expiration time. Mirrors the Linux kernel's timerqueue
// subsystem (lib/timerqueue.c, include/linux/timerqueue.h).

#pragma once

#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace engine {

// TimerQueueNode — base class for objects stored in a TimerQueue

// CRTP base providing the node interface. T must expose:
//   TimePoint expire_time() const   — the expiration time
template <typename Derived>
class TimerQueueNode {
	public:
	TimePoint expire_time() const {
		return static_cast<const Derived*>(this)->expire_time();
	}
};

// TimerQueue — rbtree-backed (std::multimap) priority queue of timer nodes
//
// Provides O(log n) insert, O(log n) delete, O(1) get-min.
// Multiple timers can share the same expiration time.

template <typename NodeT>
class TimerQueue {
	public:
	using NodePtr = NodeT*;
	using ConstNodePtr = const NodeT*;

	TimerQueue() = default;
	~TimerQueue() = default;

	// Non-copyable, movable
	TimerQueue(const TimerQueue&) = delete;
	TimerQueue& operator=(const TimerQueue&) = delete;
	TimerQueue(TimerQueue&&) = default;
	TimerQueue& operator=(TimerQueue&&) = default;

	//-----------------------------------------------------------------
	// Core operations
	//-----------------------------------------------------------------

	// Add a node. Returns true if this node is the earliest expiring.
	bool add(NodePtr node) {
		assert(node && "Cannot add null node to TimerQueue");
		TimePoint expires = node->expire_time();
		bool was_empty = empty();
		TimePoint first_expire = was_empty ? kTimeMax : first_expiry();

		auto it = tree_.emplace(expires, node);
		node->set_queue_iterator(it);  // store for O(log n) removal

		return was_empty || expires < first_expire;
	}

	// Remove a node. Returns true if the queue is non-empty after removal.
	// The node must have been added via add() — calling remove() on a node
	// that was never added is undefined behaviour.
	bool remove(NodePtr node) {
		assert(node && "Cannot remove null node from TimerQueue");
		auto it = node->queue_iterator();
		assert(it != tree_.end() && "remove() called on node not in queue");
		tree_.erase(it);
		node->clear_queue_iterator();
		return !empty();
	}

	// Get the earliest expiring node, or nullptr if empty.
	NodePtr top() const {
		if (tree_.empty()) return nullptr;
		return tree_.begin()->second;
	}

	// Get the earliest expiration time.
	TimePoint first_expiry() const {
		if (tree_.empty()) return kTimeMax;
		return tree_.begin()->first;
	}

	// Pop the earliest node (removes it from the queue).
	NodePtr pop() {
		if (tree_.empty()) return nullptr;
		auto it = tree_.begin();
		NodePtr node = it->second;
		node->clear_queue_iterator();
		tree_.erase(it);
		return node;
	}

	//-----------------------------------------------------------------
	// Query operations
	//-----------------------------------------------------------------

	bool empty() const {
		return tree_.empty();
	}
	size_t size() const {
		return tree_.size();
	}
	void clear() {
		tree_.clear();
	}

	// Collect all nodes that expire at or before the given time.
	std::vector<NodePtr> collect_expired(TimePoint until) {
		std::vector<NodePtr> result;
		auto it = tree_.begin();
		while (it != tree_.end() && it->first <= until) {
			NodePtr node = it->second;
			node->clear_queue_iterator();
			result.push_back(node);
			it = tree_.erase(it);
		}
		return result;
	}

	// Iterate over all nodes in expiration order (read-only).
	template <typename Fn>
	void for_each(Fn&& fn) const {
		for (const auto& [time, node] : tree_) {
			fn(node);
		}
	}

	// Find next timer that expires after 'time', excluding 'exclude'
	NodePtr next_expiring(TimePoint after, NodePtr exclude = nullptr) const {
		auto it = tree_.upper_bound(after);
		while (it != tree_.end()) {
			if (it->second != exclude) return it->second;
			++it;
		}
		return nullptr;
	}

	//-----------------------------------------------------------------
	// Debug / introspection
	//-----------------------------------------------------------------

	std::vector<TimePoint> all_expiry_times() const {
		std::vector<TimePoint> result;
		result.reserve(tree_.size());
		for (const auto& [time, _] : tree_) {
			result.push_back(time);
		}
		return result;
	}

	private:
	// Internal: std::multimap (rbtree) maps TimePoint -> Node*
	// We use multimap because multiple timers can expire at the same time.
	using MapType = std::multimap<TimePoint, NodePtr>;
	MapType tree_;
};

// TimerQueueLinked — variant with linked-list traversal support
// (mirrors timerqueue_linked in Linux)

template <typename NodeT>
class TimerQueueLinked {
	public:
	using NodePtr = NodeT*;

	TimerQueueLinked() = default;

	bool add(NodePtr node) {
		assert(node);
		bool was_empty = empty();
		TimePoint expires = node->expire_time();
		auto it = tree_.emplace(expires, node);
		node->set_linked_queue_iterator(it);
		return was_empty || expires < first_expiry();
	}

	bool remove(NodePtr node) {
		assert(node);
		auto it = node->linked_queue_iterator();
		assert(it != tree_.end() && "remove() called on node not in queue");
		tree_.erase(it);
		node->clear_linked_queue_iterator();
		return !empty();
	}

	NodePtr top() const {
		if (tree_.empty()) return nullptr;
		return tree_.begin()->second;
	}

	NodePtr first() const {
		return top();
	}

	// Iteration support (linked-list style)
	NodePtr next(NodePtr node) const {
		auto it = node->linked_queue_iterator();
		++it;
		if (it == tree_.end()) return nullptr;
		return it->second;
	}

	NodePtr prev(NodePtr node) const {
		auto it = node->linked_queue_iterator();
		if (it == tree_.begin()) return nullptr;
		--it;
		return it->second;
	}

	TimePoint first_expiry() const {
		if (tree_.empty()) return kTimeMax;
		return tree_.begin()->first;
	}

	bool empty() const {
		return tree_.empty();
	}
	size_t size() const {
		return tree_.size();
	}
	void clear() {
		tree_.clear();
	}

	std::vector<NodePtr> collect_expired(TimePoint until) {
		std::vector<NodePtr> result;
		auto it = tree_.begin();
		while (it != tree_.end() && it->first <= until) {
			NodePtr node = it->second;
			node->clear_linked_queue_iterator();
			result.push_back(node);
			it = tree_.erase(it);
		}
		return result;
	}

	NodePtr next_expiring(TimePoint after, NodePtr exclude = nullptr) const {
		auto it = tree_.upper_bound(after);
		while (it != tree_.end()) {
			if (it->second != exclude) return it->second;
			++it;
		}
		return nullptr;
	}

	template <typename Fn>
	void for_each(Fn&& fn) const {
		for (const auto& [time, node] : tree_) {
			fn(node);
		}
	}

	private:
	using MapType = std::multimap<TimePoint, NodePtr>;
	MapType tree_;
};

}  // namespace engine
