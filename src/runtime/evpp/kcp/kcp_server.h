#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "runtime/evpp/inner_pre.h"
#include "runtime/evpp/kcp/kcp_message.h"
#include "runtime/evpp/thread_dispatch_policy.h"

namespace evpp {

class EventLoopThreadPool;
class EventLoop;

namespace kcp {

class EVPP_EXPORT Server : public ThreadDispatchPolicy {
	public:
	typedef std::function<void(EventLoop*, MessagePtr& msg)> MessageHandler;

	public:
	Server();
	~Server();

	bool Init(int port);
	bool Init(const std::vector<int>& ports);
	bool Init(const std::string& listen_ports /*like "53,5353,1053"*/);
	bool Start();
	void Stop(bool wait_thread_exit);

	void Pause();
	void Continue();

	// @brief Reinitialize some data fields after a fork
	void AfterFork();

	bool IsRunning() const;
	bool IsStopped() const;

	void SetMessageHandler(MessageHandler handler) {
		message_handler_ = handler;
	}

	void SetEventLoopThreadPool(const std::shared_ptr<EventLoopThreadPool>& pool) {
		tpool_ = pool;
	}

	// KCP tuning parameters (applied to new sessions; see ikcp.h for details)
	// @param nodelay 0:disable(default), 1:enable
	// @param interval internal update timer interval in ms (KCP default 100, server default 10)
	// @param resend fast retransmit ACK count (0:disable, server default 2)
	// @param nc 0:normal congestion control(default), 1:disable congestion control
	void SetKcpNodelay(int nodelay, int interval, int resend, int nc);

	// @param sndwnd send window size, default 128
	// @param rcvwnd recv window size, default 128
	void SetKcpWndSize(int sndwnd, int rcvwnd);

	// @param mtu maximum transmission unit, default 1400
	void SetKcpMtu(int mtu);

	// @param timeout_ms max idle time before a session is cleaned up, default 30000ms
	void SetSessionTimeoutMs(uint32_t timeout_ms);

	private:
	class KcpSession;
	class RecvThread;
	typedef std::shared_ptr<RecvThread> RecvThreadPtr;
	typedef std::shared_ptr<KcpSession> KcpSessionPtr;

	std::vector<RecvThreadPtr> recv_threads_;

	MessageHandler message_handler_;

	// The worker thread pool, used to process KCP application messages.
	// This field is not owned by the server — it is set by the outer application.
	std::shared_ptr<EventLoopThreadPool> tpool_;

	// KCP parameters shared across all sessions
	int kcp_nodelay_ = 1;
	int kcp_interval_ = 10;
	int kcp_resend_ = 2;
	int kcp_nc_ = 1;  // disable congestion control by default
	int kcp_sndwnd_ = 128;
	int kcp_rcvwnd_ = 128;
	int kcp_mtu_ = 1400;

	// Max idle time before a KCP session is considered stale (ms)
	uint32_t session_timeout_ms_ = 30000;

	void RecvingLoop(RecvThread* th);
};

}  // namespace kcp
}  // namespace evpp
