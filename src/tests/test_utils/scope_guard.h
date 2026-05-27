#pragma once

#include <functional>
#include <utility>

namespace test {

// RAII scope guard — runs a callback on destruction unless dismissed.
// Useful for ensuring cleanup in test teardown without try/catch.
//
// Usage:
//   auto guard = ScopeGuard([]() { cleanup(); });
//   // ... test body ...
//   guard.Dismiss();  // optional: skip cleanup on success
template <typename F>
class ScopeGuard {
public:
	explicit ScopeGuard(F&& f) : callback_(std::forward<F>(f)), active_(true) {}
	ScopeGuard(ScopeGuard&& other) noexcept
		: callback_(std::move(other.callback_)), active_(other.active_) {
		other.Dismiss();
	}
	ScopeGuard& operator=(ScopeGuard&& other) noexcept {
		if (this != &other) {
			if (active_) callback_();
			callback_ = std::move(other.callback_);
			active_ = other.active_;
			other.Dismiss();
		}
		return *this;
	}

	ScopeGuard(const ScopeGuard&) = delete;
	ScopeGuard& operator=(const ScopeGuard&) = delete;

	~ScopeGuard() { if (active_) callback_(); }

	void Dismiss() { active_ = false; }

private:
	F callback_;
	bool active_;
};

template <typename F>
ScopeGuard<F> MakeScopeGuard(F&& f) {
	return ScopeGuard<F>(std::forward<F>(f));
}

}  // namespace test
