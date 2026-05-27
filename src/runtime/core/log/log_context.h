#pragma once

#include <cstdint>
#include <string>

namespace engine {

class TraceContext {
	public:
	static void set_trace_id(std::string id) {
		trace_id_ = std::move(id);
	}
	static const std::string& trace_id() {
		return trace_id_;
	}

	static void set_player_id(uint64_t id) {
		player_id_ = id;
	}
	static uint64_t player_id() {
		return player_id_;
	}

	static void set_room_id(uint64_t id) {
		room_id_ = id;
	}
	static uint64_t room_id() {
		return room_id_;
	}

	static void clear() {
		trace_id_.clear();
		player_id_ = 0;
		room_id_ = 0;
	}

	private:
	thread_local static inline std::string trace_id_;
	thread_local static inline uint64_t player_id_{0};
	thread_local static inline uint64_t room_id_{0};
};

}  // namespace engine
