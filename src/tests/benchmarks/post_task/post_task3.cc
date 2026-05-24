#include <evpp/event_loop.h>
#include <evpp/event_loop_thread.h>

#include "runtime/core/log/log.h"

#include "tests/examples/winmain-inl.h"

uint64_t clock_us() {
    return std::chrono::steady_clock::now().time_since_epoch().count() / 1000;
}

class PostTask {
public:
    PostTask(uint64_t post_count)
        : post_count_(post_count) {
    }

    void Start() {
        loop1_.Start(true);
        loop2_.Start(true);
        loop1_.loop()->RunInLoop([this]() {
            start_time_ = clock_us();
            for (size_t i = 0; i < post_count_; ++i) {
                loop2_.loop()->RunInLoop([this]() {
                    count_ += 1;
                    if (count_ == post_count_) {
                        stop();
                    }
                });
            }
        });

    }

    void Wait() {
        while (!loop1_.IsStopped() && !loop2_.IsStopped()) {
            usleep(500000);
        }
    }


    double use_time() const {
        return double(stop_time_ - start_time_) / 1000000.0;
    }
private:
    void stop() {
        stop_time_ = clock_us();
        loop1_.Stop();
        loop2_.Stop();
    }
private:
    uint64_t const post_count_;
    evpp::EventLoopThread loop1_; // send task
    evpp::EventLoopThread loop2_; // execute task
    uint64_t count_ = 0;
    uint64_t start_time_;
    uint64_t stop_time_;
};

int main(int argc, char* argv[]) {
    long long post_count = 10000;

    if (argc == 2) {
        post_count = std::atoll(argv[1]);
    } else {
        printf("Usage : %s <post-count>\n", argv[0]);
        return 0;
    }

    PostTask p(post_count);
    p.Start();
    p.Wait();
    ENGINE_LOG_WARN(engine::GetLogger(), "{} post_count={} use time: {} seconds\n", argv[0], post_count, p.use_time());
    return 0;
}
