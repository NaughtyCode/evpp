#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "runtime/evpp/buffer.h"
#include "runtime/evpp/slice.h"
#include "runtime/evpp/tcp_callbacks.h"

namespace test {

// A fake TCP connection that buffers sent data and allows manual message
// delivery. No real sockets — fully synchronous and deterministic.
//
// Usage:
//   FakeTCPConnection conn;
//   conn.SetMessageCallback([](const evpp::TCPConnPtr&, evpp::Buffer* buf) { ... });
//   conn.SimulateConnect();
//   conn.Send("hello");
//   ASSERT_EQ(conn.SentData().size(), 1);
class FakeTCPConnection {
public:
	FakeTCPConnection() = default;
	explicit FakeTCPConnection(const std::string& name) : name_(name) {}

	// ── Connection state ───────────────────────────────────────────────

	enum Status { kDisconnected = 0, kConnected = 2 };

	void SimulateConnect() {
		status_ = kConnected;
		if (conn_callback_) conn_callback_();
	}

	void SimulateDisconnect() {
		status_ = kDisconnected;
		if (close_callback_) close_callback_();
	}

	void Close() {
		status_ = kDisconnected;
		if (close_callback_) close_callback_();
	}

	bool IsConnected() const { return status_ == kConnected; }
	Status status() const { return status_; }
	const std::string& name() const { return name_; }
	void set_name(const std::string& n) { name_ = n; }

	// ── Sending ────────────────────────────────────────────────────────

	void Send(const char* s) { Send(s, strlen(s)); }

	void Send(const void* d, size_t dlen) {
		sent_data_.emplace_back(static_cast<const char*>(d), dlen);
		total_bytes_sent_ += dlen;
	}

	void Send(const std::string& d) {
		sent_data_.push_back(d);
		total_bytes_sent_ += d.size();
	}

	void Send(const evpp::Slice& msg) {
		Send(msg.data(), msg.size());
	}

	void Send(evpp::Buffer* buf) {
		Send(buf->data(), buf->length());
	}

	// ── Receiving (manual delivery from test) ──────────────────────────

	// Simulate receiving data from the remote peer.
	void DeliverMessage(const void* data, size_t len) {
		if (msg_callback_) {
			evpp::Buffer buf;
			buf.Append(data, len);
			msg_callback_(nullptr, &buf);  // conn ptr unused in fake
		}
	}

	void DeliverMessage(const std::string& data) {
		DeliverMessage(data.data(), data.size());
	}

	// ── Callbacks ──────────────────────────────────────────────────────

	void SetMessageCallback(std::function<void(evpp::Buffer*)> cb) {
		msg_callback_ = std::move(cb);
	}

	void SetConnectionCallback(std::function<void()> cb) {
		conn_callback_ = std::move(cb);
	}

	void SetCloseCallback(std::function<void()> cb) {
		close_callback_ = std::move(cb);
	}

	// ── Test introspection ─────────────────────────────────────────────

	const std::vector<std::string>& SentData() const { return sent_data_; }

	std::string LastSentData() const {
		return sent_data_.empty() ? "" : sent_data_.back();
	}

	size_t TotalBytesSent() const { return total_bytes_sent_; }

	void Clear() {
		sent_data_.clear();
		total_bytes_sent_ = 0;
	}

private:
	using MessageCallback = std::function<void(evpp::Buffer*)>;
	using VoidCallback = std::function<void()>;

	std::string name_ = "fake_conn";
	Status status_ = kDisconnected;

	std::vector<std::string> sent_data_;
	size_t total_bytes_sent_ = 0;

	MessageCallback msg_callback_;
	VoidCallback conn_callback_;
	VoidCallback close_callback_;
};

}  // namespace test
