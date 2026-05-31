#include "runtime/evpp/tcp_conn.h"

#include <chrono>

#include "runtime/evpp/event_loop.h"
#include "runtime/evpp/fd_channel.h"
#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/invoke_timer.h"
#include "runtime/evpp/libevent.h"
#include "runtime/evpp/sockets.h"
#include "runtime/monitoring/metrics.h"

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace evpp {
TCPConn::TCPConn(EventLoop* l,
				 const std::string& n,
				 evpp_socket_t sockfd,
				 const std::string& laddr,
				 const std::string& raddr,
				 uint64_t conn_id)
	: loop_(l),
	  fd_(sockfd),
	  id_(conn_id),
	  name_(n),
	  local_addr_(laddr),
	  remote_addr_(raddr),
	  type_(kIncoming),
	  status_(kDisconnected) {
	if (sockfd >= 0) {
		chan_.reset(CLOUDENGINE_MEM_NEW(FdChannel, l, sockfd, false, false));
		chan_->SetReadCallback(std::bind(&TCPConn::HandleRead, this));
		chan_->SetWriteCallback(std::bind(&TCPConn::HandleWrite, this));
	}

	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} TCPConn::[{}] channel={} fd={} addr={}",
					 (void*) this,
					 name_,
					 (void*) chan_.get(),
					 sockfd,
					 AddrToString());
}

TCPConn::~TCPConn() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} name={} channel={} fd={} type={} status={} addr={}",
					 (void*) this,
					 name(),
					 (void*) chan_.get(),
					 fd_,
					 int(type()),
					 StatusToString(),
					 AddrToString());
	assert(status_ == kDisconnected);

	if (fd_ >= 0) {
		assert(chan_);
		assert(fd_ == chan_->fd());
		assert(chan_->IsNoneEvent());
		EVUTIL_CLOSESOCKET(fd_);
		fd_ = INVALID_SOCKET;
	}

	assert(!delay_close_timer_.get());
}

void TCPConn::Close() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} fd={} status={} addr={}",
					 (void*) this,
					 fd_,
					 StatusToString(),
					 AddrToString());
	if (status_ == kDisconnected || status_ == kDisconnecting) {
		return;
	}
	status_ = kDisconnecting;
	auto c = shared_from_this();
	auto f = [c]() {
		assert(c->loop_->IsInLoopThread());
		c->HandleClose();
	};

	// Use QueueInLoop to fix TCPClient::Close bug when the application delete TCPClient in callback
	if (loop_->IsInLoopThread() && loop_->IsStopped()) {
		f();
	} else {
		loop_->QueueInLoop(f);
	}
}

void TCPConn::Send(const std::string& d) {
	if (status_ != kConnected) {
		return;
	}

	if (loop_->IsInLoopThread()) {
		SendInLoop(d);
	} else {
		loop_->RunInLoop(std::bind(&TCPConn::SendStringInLoop, shared_from_this(), d));
	}
}

void TCPConn::Send(const Slice& message) {
	if (status_ != kConnected) {
		return;
	}

	if (loop_->IsInLoopThread()) {
		SendInLoop(message);
	} else {
		loop_->RunInLoop(
			std::bind(&TCPConn::SendStringInLoop, shared_from_this(), message.ToString()));
	}
}

void TCPConn::Send(const void* data, size_t len) {
	if (status_ != kConnected) {
		ENGINE_LOG_WARN(engine::GetLogger(), "Send dropped: connection {} not connected", id_);
		return;
	}

	if (loop_->IsInLoopThread()) {
		SendInLoop(data, len);
		return;
	}
	Send(Slice(static_cast<const char*>(data), len));
}

void TCPConn::Send(const void* data, size_t len, MessagePriority priority) {
	if (status_ != kConnected) {
		ENGINE_LOG_WARN(engine::GetLogger(), "Send dropped: connection {} not connected", id_);
		return;
	}
	if (len == 0 || data == nullptr) {
		return;
	}

	std::string message(static_cast<const char*>(data), len);

	if (loop_->IsInLoopThread()) {
		SendPriorityInLoop(std::move(message), priority);
		return;
	}
	auto self = shared_from_this();
	loop_->RunInLoop([self, message = std::move(message), priority]() mutable {
		self->SendPriorityInLoop(std::move(message), priority);
	});
}

void TCPConn::Send(Buffer* buf) {
	if (status_ != kConnected) {
		return;
	}

	if (loop_->IsInLoopThread()) {
		SendInLoop(buf->data(), buf->length());
		buf->Reset();
	} else {
		loop_->RunInLoop(
			std::bind(&TCPConn::SendStringInLoop, shared_from_this(), buf->NextAllString()));
	}
}

void TCPConn::SendInLoop(const Slice& message) {
	SendInLoop(message.data(), message.size());
}

void TCPConn::SendStringInLoop(const std::string& message) {
	SendInLoop(message.data(), message.size());
}

bool TCPConn::SendPriorityInLoop(std::string data, MessagePriority priority) {
	assert(loop_->IsInLoopThread());

	if (status_ != kConnected || data.empty()) {
		return false;
	}

	const uint32_t request_size = data.size() > UINT32_MAX
									  ? UINT32_MAX
									  : static_cast<uint32_t>(data.size());
	uint32_t allowed = rate_limiter_.Consume(request_size);
	if (allowed < data.size()) {
		pending_messages_.push({priority,
								data.substr(static_cast<size_t>(allowed)),
								std::chrono::duration_cast<std::chrono::nanoseconds>(
									std::chrono::steady_clock::now().time_since_epoch()).count()});
		SchedulePendingFlush();
	}
	if (allowed == 0) {
		return false;
	}

	SendInLoop(data.data(), static_cast<size_t>(allowed));
	return true;
}

void TCPConn::SendInLoop(const void* data, size_t len) {
	assert(loop_->IsInLoopThread());

	if (status_ == kDisconnected) {
		ENGINE_LOG_WARN(engine::GetLogger(), "disconnected, give up writing");
		return;
	}

	ssize_t nwritten = 0;
	size_t remaining = len;
	bool write_error = false;

	// if no data in output queue, writing directly
	if (!chan_->IsWritable() && output_buffer_.length() == 0) {
#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)
		if (ssl_) {
			int ssl_ret = SSL_write(ssl_, data, static_cast<int>(len));
			if (ssl_ret > 0) {
				nwritten = ssl_ret;
				remaining = len - nwritten;
				if (remaining == 0 && write_complete_fn_) {
					loop_->QueueInLoop(std::bind(write_complete_fn_, shared_from_this()));
				}
			} else {
				int ssl_err = SSL_get_error(ssl_, ssl_ret);
				nwritten = 0;
				if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
					// Normal for non-blocking — buffer and re-enable write
				} else if (ssl_err == SSL_ERROR_ZERO_RETURN) {
					ENGINE_LOG_TRACE(engine::GetLogger(), "SSL connection closed by peer");
					write_error = true;
				} else {
					ENGINE_LOG_ERROR(engine::GetLogger(),
									 "SSL_write failed: {}",
									 ERR_error_string(ERR_get_error(), nullptr));
					write_error = true;
				}
			}
		} else
#endif
		{
			nwritten = ::send(chan_->fd(), static_cast<const char*>(data), len, MSG_NOSIGNAL);
			if (nwritten >= 0) {
				remaining = len - nwritten;
				if (remaining == 0 && write_complete_fn_) {
					loop_->QueueInLoop(std::bind(write_complete_fn_, shared_from_this()));
				}
			} else {
				int serrno = EVPP_ERRNO;
				nwritten = 0;
				if (!EVUTIL_ERR_RW_RETRIABLE(serrno)) {
					ENGINE_LOG_ERROR(engine::GetLogger(),
									 "SendInLoop write failed errno={} {}",
									 serrno,
									 strerror(serrno));
					write_error = true;
				}
			}
		}
	}

	if (write_error) {
		HandleError();
		return;
	}

	assert(!write_error);
	assert(remaining <= len);

	if (remaining > 0) {
		size_t old_len = output_buffer_.length();
		if (old_len + remaining >= high_water_mark_ && old_len < high_water_mark_ &&
			high_water_mark_fn_) {
			loop_->QueueInLoop(
				std::bind(high_water_mark_fn_, shared_from_this(), old_len + remaining));
		}

		output_buffer_.Append(static_cast<const char*>(data) + nwritten, remaining);

		if (!chan_->IsWritable()) {
			chan_->EnableWriteEvent();
		}
	}
	if (len > 0) {
		auto& metrics = engine::monitoring::MetricsRegistry::Instance();
		metrics.messages_sent_total().Inc();
		metrics.GetHistogram("evpp_message_size_bytes", {64, 256, 1024, 4096, 16384, 65536})
			.Observe(static_cast<double>(len));
	}
}

void TCPConn::HandleRead() {
	assert(loop_->IsInLoopThread());
	int serrno = 0;
	ssize_t n = 0;
#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)
	if (ssl_) {
		input_buffer_.EnsureWritableBytes(65536);
		int ssl_ret = SSL_read(ssl_, input_buffer_.WriteBegin(),
							   static_cast<int>(input_buffer_.WritableBytes()));
		if (ssl_ret > 0) {
			n = ssl_ret;
			input_buffer_.WriteBytes(static_cast<size_t>(n));
			msg_fn_(shared_from_this(), &input_buffer_);
			return;
		}
		int ssl_err = SSL_get_error(ssl_, ssl_ret);
		if (ssl_err == SSL_ERROR_WANT_READ) {
			return;
		}
		if (ssl_err == SSL_ERROR_WANT_WRITE) {
			chan_->EnableWriteEvent();
			return;
		}
		if (ssl_err == SSL_ERROR_ZERO_RETURN) {
			n = 0;  // Clean shutdown
		} else {
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "SSL_read failed: {}",
							 ERR_error_string(ERR_get_error(), nullptr));
			HandleError();
			return;
		}
	} else
#endif
	{
		n = input_buffer_.ReadFromFD(chan_->fd(), &serrno);
	}
	if (n > 0) {
		auto& metrics = engine::monitoring::MetricsRegistry::Instance();
		metrics.messages_received_total().Inc();
		metrics.GetHistogram("evpp_message_size_bytes", {64, 256, 1024, 4096, 16384, 65536})
			.Observe(static_cast<double>(n));
		msg_fn_(shared_from_this(), &input_buffer_);
	} else if (n == 0) {
		if (type() == kOutgoing) {
			ENGINE_LOG_TRACE(engine::GetLogger(),
							 "this={} fd={}. We read 0 bytes and close the socket.",
							 (void*) this,
							 fd_);
			status_ = kDisconnecting;
			HandleClose();
		} else {
			chan_->DisableReadEvent();
			if (close_delay_.IsZero()) {
				ENGINE_LOG_TRACE(engine::GetLogger(),
								 "this={} channel (fd={}) DisableReadEvent. delay time {}s. We "
								 "close this connection immediately",
								 (void*) this,
								 chan_->fd(),
								 close_delay_.Seconds());
				DelayClose();
			} else {
				ENGINE_LOG_TRACE(engine::GetLogger(),
								 "this={} channel (fd={}) DisableReadEvent. And set a timer to "
								 "delay close this TCPConn, delay time {}s",
								 (void*) this,
								 chan_->fd(),
								 close_delay_.Seconds());
				delay_close_timer_ = loop_->RunAfter(
					close_delay_,
					std::bind(&TCPConn::DelayClose,
							  shared_from_this()));
			}
		}
	} else {
		if (EVUTIL_ERR_RW_RETRIABLE(serrno)) {
			ENGINE_LOG_TRACE(
				engine::GetLogger(), "this={} errno={} {}", (void*) this, serrno, strerror(serrno));
		} else {
			ENGINE_LOG_TRACE(engine::GetLogger(),
							 "this={} errno={} {} We are closing this connection now.",
							 (void*) this,
							 serrno,
							 strerror(serrno));
			HandleError();
		}
	}
}

void TCPConn::HandleWrite() {
	assert(loop_->IsInLoopThread());
	assert(!chan_->attached() || chan_->IsWritable());

#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)
	if (ssl_) {
		// If no data to write, try advancing the SSL handshake
		if (output_buffer_.length() == 0) {
			int hs_ret = SSL_do_handshake(ssl_);
			if (hs_ret == 1) {
				chan_->DisableWriteEvent();
				return;
			}
			int hs_err = SSL_get_error(ssl_, hs_ret);
			if (hs_err == SSL_ERROR_WANT_READ) {
				chan_->DisableWriteEvent();
				return;
			}
			if (hs_err == SSL_ERROR_WANT_WRITE) {
				return;
			}
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "SSL handshake failed: {}",
							 ERR_error_string(ERR_get_error(), nullptr));
			HandleError();
			return;
		}

		int ssl_ret = SSL_write(ssl_, output_buffer_.data(),
								static_cast<int>(output_buffer_.length()));
		if (ssl_ret > 0) {
			output_buffer_.Next(static_cast<size_t>(ssl_ret));
			if (output_buffer_.length() == 0) {
				chan_->DisableWriteEvent();
				while (!pending_messages_.empty()) {
					PendingMessage msg = pending_messages_.top();
					pending_messages_.pop();
					if (!SendPriorityInLoop(std::move(msg.data), msg.priority)) {
						break;
					}
					if (output_buffer_.length() > 0) {
						chan_->EnableWriteEvent();
						break;
					}
				}
				if (write_complete_fn_ && pending_messages_.empty()) {
					loop_->QueueInLoop(std::bind(write_complete_fn_, shared_from_this()));
				}
			}
		} else {
			int ssl_err = SSL_get_error(ssl_, ssl_ret);
			if (ssl_err == SSL_ERROR_WANT_WRITE || ssl_err == SSL_ERROR_WANT_READ) {
				return;  // Retry later
			}
			ENGINE_LOG_ERROR(engine::GetLogger(),
							 "SSL_write HandleWrite failed: {}",
							 ERR_error_string(ERR_get_error(), nullptr));
			HandleError();
		}
		return;
	}
#endif

	ssize_t n = ::send(fd_, output_buffer_.data(), output_buffer_.length(), MSG_NOSIGNAL);
	if (n > 0) {
		output_buffer_.Next(n);

		if (output_buffer_.length() == 0) {
			chan_->DisableWriteEvent();

			// Drain priority queue before firing write-complete callback
			while (!pending_messages_.empty()) {
				PendingMessage msg = pending_messages_.top();
				pending_messages_.pop();
				if (!SendPriorityInLoop(std::move(msg.data), msg.priority)) {
					break;
				}
				if (output_buffer_.length() > 0) {
					chan_->EnableWriteEvent();
					break;
				}
			}

			if (write_complete_fn_ && pending_messages_.empty()) {
				loop_->QueueInLoop(std::bind(write_complete_fn_, shared_from_this()));
			}
		}
	} else {
		int serrno = EVPP_ERRNO;

		if (EVUTIL_ERR_RW_RETRIABLE(serrno)) {
			ENGINE_LOG_WARN(engine::GetLogger(),
							"this={} TCPConn::HandleWrite errno={} {}",
							(void*) this,
							serrno,
							strerror(serrno));
		} else {
			HandleError();
		}
	}
}

void TCPConn::FlushPendingMessages() {
	assert(loop_->IsInLoopThread());
	pending_flush_timer_.reset();

	if (status_ != kConnected) {
		while (!pending_messages_.empty()) {
			pending_messages_.pop();
		}
		return;
	}

	while (output_buffer_.length() == 0 && !pending_messages_.empty()) {
		PendingMessage msg = pending_messages_.top();
		pending_messages_.pop();
		if (!SendPriorityInLoop(std::move(msg.data), msg.priority)) {
			break;
		}
	}

	if (!pending_messages_.empty() && output_buffer_.length() == 0) {
		SchedulePendingFlush();
	}
}

void TCPConn::SchedulePendingFlush() {
	assert(loop_->IsInLoopThread());
	if (pending_flush_timer_ || pending_messages_.empty() || status_ != kConnected) {
		return;
	}

	pending_flush_timer_ = loop_->RunAfter(
		Duration(0.001),
		std::bind(&TCPConn::FlushPendingMessages, shared_from_this()));
}

void TCPConn::DelayClose() {
	assert(loop_->IsInLoopThread());
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} addr={} fd={} status_={}",
					 (void*) this,
					 AddrToString(),
					 fd_,
					 StatusToString());
	status_ = kDisconnecting;
	delay_close_timer_.reset();
	HandleClose();
}

void TCPConn::HandleClose() {
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} addr={} fd={} status_={}",
					 (void*) this,
					 AddrToString(),
					 fd_,
					 StatusToString());

	// Avoid multi calling
	if (status_ == kDisconnected) {
		return;
	}

	// We call HandleClose() from TCPConn's method, the status_ is kConnected
	// But we call HandleClose() from out of TCPConn's method, the status_ is kDisconnecting
	assert(status_ == kDisconnecting);

	// This setting is required, it indicates connecting state and must not be removed
	status_ = kDisconnecting;
	assert(loop_->IsInLoopThread());
#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)
	if (ssl_) {
		SSL_shutdown(ssl_);
		SSL_free(ssl_);
		ssl_ = nullptr;
	}
#endif
	if (chan_) {
		chan_->DisableAllEvent();
		chan_->Close();
	}

	TCPConnPtr conn(shared_from_this());

	if (delay_close_timer_) {
		ENGINE_LOG_TRACE(engine::GetLogger(),
						 "this={} loop={} Cancel the delay closing timer.",
						 (void*) this,
						 (void*) loop_);
		delay_close_timer_->Cancel();
		delay_close_timer_.reset();
	}
	if (pending_flush_timer_) {
		pending_flush_timer_->Cancel();
		pending_flush_timer_.reset();
	}
	while (!pending_messages_.empty()) {
		pending_messages_.pop();
	}

	if (conn_fn_) {
		// This callback must be invoked at status kDisconnecting
		// e.g. when the TCPClient disconnects with remote server,
		// the user layer can decide whether to do the reconnection.
		assert(status_ == kDisconnecting);
		conn_fn_(conn);
	}

	if (close_fn_) {
		close_fn_(conn);
	}
	ENGINE_LOG_TRACE(engine::GetLogger(),
					 "this={} addr={} fd={} status_={} use_count={}",
					 (void*) this,
					 AddrToString(),
					 fd_,
					 StatusToString(),
					 conn.use_count());
	status_ = kDisconnected;
}

void TCPConn::HandleError() {
	ENGINE_LOG_TRACE(
		engine::GetLogger(), "this={} fd={} status={}", (void*) this, fd_, StatusToString());
	status_ = kDisconnecting;
	HandleClose();
}

void TCPConn::OnAttachedToLoop() {
	assert(loop_->IsInLoopThread());
	status_ = kConnected;
#if defined(EVPP_HTTP_CLIENT_SUPPORTS_SSL) || defined(EVPP_OPENSSL_ENABLED)
	if (ssl_ctx_) {
		ssl_ = SSL_new(ssl_ctx_);
		if (ssl_) {
			SSL_set_fd(ssl_, fd_);
			if (type_ == kIncoming) {
				SSL_set_accept_state(ssl_);
			} else {
				SSL_set_connect_state(ssl_);
			}
		}
	}
#endif
	chan_->EnableReadEvent();

	if (conn_fn_) {
		conn_fn_(shared_from_this());
	}
}

void TCPConn::SetHighWaterMarkCallback(const HighWaterMarkCallback& cb, size_t mark) {
	high_water_mark_fn_ = cb;
	high_water_mark_ = mark;
}

void TCPConn::SetTCPNoDelay(bool on) {
	sock::SetTCPNoDelay(fd_, on);
}

void TCPConn::SetLinger(bool on, int seconds) {
	sock::SetLinger(fd_, on, seconds);
}

std::string TCPConn::StatusToString() const {
	H_CASE_STRING_BIGIN(status_.load());
	H_CASE_STRING(kDisconnected);
	H_CASE_STRING(kConnecting);
	H_CASE_STRING(kConnected);
	H_CASE_STRING(kDisconnecting);
	H_CASE_STRING_END();
}
}
