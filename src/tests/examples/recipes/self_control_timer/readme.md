# evpp Design Details Series (1): Implementing a Self-Managed Timer Using enable_shared_from_this



# 0. Foreword

[https://github.com/Qihoo360/evpp](https://github.com/Qihoo360/evpp) is a high-performance, modern C++11 network library based on the Reactor pattern. The project includes an `InvokeTimer` object — for the detailed interface header file, see [https://github.com/Qihoo360/evpp/blob/master/evpp/invoke_timer.h](https://github.com/Qihoo360/evpp/blob/master/evpp/invoke_timer.h). It is a self-managing timer class that can bind a functor to the timer and then let the timer manage itself and execute the functor after a specified period.

Now let's review the implementation details and evolution of this feature.

# 1. Base Code

The timer prototype declaration might look like this:

```C++
class InvokeTimer {
public:
    InvokeTimer(struct event_base* evloop, double timeout_ms, const std::function<void()>& f);
    ~InvokeTimer();
    void Start();
};
```

This is the most basic interface: you can set a functor with an expiration time, bind it to an `event_base` object, and then expect the functor to be called after the specified time has elapsed.

To facilitate explaining the multiple versions that follow, let's first explain the foundational code that remains unchanged.

For the base code, we use the `TimerEventWatcher` from the [evpp] project, with detailed implementation in [event_watcher.h] and [event_watcher.cc]. It is a timer event watcher object that can observe a timer event.

The header file `event_watcher.h` is defined as follows:

```C++
#pragma once

#include <functional>

struct event;
struct event_base;

namespace recipes {

class EventWatcher {
public:
    typedef std::function<void()> Handler;
    virtual ~EventWatcher();
    bool Init();
    void Cancel();

    void SetCancelCallback(const Handler& cb);
    void ClearHandler() { handler_ = Handler(); }
protected:
    EventWatcher(struct event_base* evbase, const Handler& handler);
    bool Watch(double timeout_ms);
    void Close();
    void FreeEvent();

    virtual bool DoInit() = 0;
    virtual void DoClose() {}

protected:
    struct event* event_;
    struct event_base* evbase_;
    bool attached_;
    Handler handler_;
    Handler cancel_callback_;
};

class TimerEventWatcher : public EventWatcher {
public:
    TimerEventWatcher(struct event_base* evbase, const Handler& handler, double timeout_ms);

    bool AsyncWait();

private:
    virtual bool DoInit();
    static void HandlerFn(int fd, short which, void* v);
private:
    double timeout_ms_;
};

}

```

The implementation file `event_watcher.cc` is as follows:

```C++
#include <string.h>
#include <assert.h>

#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/event_compat.h>

#include <iostream>

#include "event_watcher.h"

namespace recipes {

EventWatcher::EventWatcher(struct event_base* evbase, const Handler& handler)
    : evbase_(evbase), attached_(false), handler_(handler) {
    event_ = new event;
    memset(event_, 0, sizeof(struct event));
}

EventWatcher::~EventWatcher() {
    FreeEvent();
    Close();
}

bool EventWatcher::Init() {
    if (!DoInit()) {
        goto failed;
    }

    ::event_base_set(evbase_, event_);
    return true;

failed:
    Close();
    return false;
}


void EventWatcher::Close() {
    DoClose();
}

bool EventWatcher::Watch(double timeout_ms) {
    struct timeval tv;
    struct timeval* timeoutval = nullptr;
    if (timeout_ms > 0) {
        tv.tv_sec = long(timeout_ms / 1000);
        tv.tv_usec = long(timeout_ms * 1000.0) % 1000;
        timeoutval = &tv;
    }

    if (attached_) {
        // When InvokerTimer::periodic_ == true, EventWatcher::Watch will be called many times
        // so we need to remove it from event_base before we add it into event_base
        if (event_del(event_) != 0) {
            std::cerr << "event_del failed. fd=" << this->event_->ev_fd << " event_=" << event_ << std::endl;
            // TODO how to deal with it when failed?
        }
        attached_ = false;
    }

    assert(!attached_);
    if (event_add(event_, timeoutval) != 0) {
        std::cerr << "event_add failed. fd=" << this->event_->ev_fd << " event_=" << event_ << std::endl;
        return false;
    }
    attached_ = true;
    return true;
}

void EventWatcher::FreeEvent() {
    if (event_) {
        if (attached_) {
            event_del(event_);
            attached_ = false;
        }

        delete (event_);
        event_ = nullptr;
    }
}

void EventWatcher::Cancel() {
    assert(event_);
    FreeEvent();

    if (cancel_callback_) {
        cancel_callback_();
        cancel_callback_ = Handler();
    }
}

void EventWatcher::SetCancelCallback(const Handler& cb) {
    cancel_callback_ = cb;
}


TimerEventWatcher::TimerEventWatcher(struct event_base* evbase,
                                     const Handler& handler,
                                     double timeout_ms)
    : EventWatcher(evbase, handler)
    , timeout_ms_(timeout_ms) {}

bool TimerEventWatcher::DoInit() {
    ::event_set(event_, -1, 0, TimerEventWatcher::HandlerFn, this);
    return true;
}

void TimerEventWatcher::HandlerFn(int /*fd*/, short /*which*/, void* v) {
    TimerEventWatcher* h = (TimerEventWatcher*)v;
    h->handler_();
}

bool TimerEventWatcher::AsyncWait() {
    return Watch(timeout_ms_);
}

}

```


# 2. A Basic Implementation: basic-01

Let's first try to implement a timer that meets the most basic requirements.

```C++

// Header file
#include <memory>
#include <functional>

struct event_base;

namespace recipes {

class TimerEventWatcher;
class InvokeTimer;

class InvokeTimer {
public:
    typedef std::function<void()> Functor;

    InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f);
    ~InvokeTimer();

    void Start();

private:
    void OnTimerTriggered();

private:
    struct event_base* loop_;
    double timeout_ms_;
    Functor functor_;
    std::shared_ptr<TimerEventWatcher> timer_;
};

}



// Implementation file
#include "invoke_timer.h"
#include "event_watcher.h"

#include <thread>
#include <iostream>

namespace recipes {

InvokeTimer::InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f)
    : loop_(evloop), timeout_ms_(timeout_ms), functor_(f) {
    std::cout << "InvokeTimer::InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
}

InvokeTimer::~InvokeTimer() {
    std::cout << "InvokeTimer::~InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
}

void InvokeTimer::Start() {
    std::cout << "InvokeTimer::Start tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
    timer_.reset(new TimerEventWatcher(loop_, std::bind(&InvokeTimer::OnTimerTriggered, this), timeout_ms_));
    timer_->Init();
    timer_->AsyncWait();
    std::cout << "InvokeTimer::Start(AsyncWait) tid=" << std::this_thread::get_id() << " timer=" << timer_.get() << " this=" << this << " timeout(ms)=" << timeout_ms_ << std::endl;
}

void InvokeTimer::OnTimerTriggered() {
    std::cout << "InvokeTimer::OnTimerTriggered tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
    functor_();
    functor_ = Functor();
}

}


```

Test main.cc

```C++
#include "invoke_timer.h"
#include "event_watcher.h"
#include "winmain-inl.h"

#include <event2/event.h>

void Print() {
    std::cout << __FUNCTION__ << " hello world." << std::endl;
}

int main() {
    struct event_base* base = event_base_new();
    auto timer = new recipes::InvokeTimer(base, 1000.0, &Print);
    timer->Start();
    event_base_dispatch(base);
    event_base_free(base);
    delete timer;
    return 0;
}
```

We first create an `event_base` object, then create an `InvokeTimer` object, then start the timer — i.e., register the timer with the `event_base` object — and finally run `event_base_dispatch(base)`.

Compile and run it. The result meets expectations: when the timer expires, the callback is triggered successfully.

```shell
$ ls -l
total 80
-rw-rw-r-- 1 weizili weizili  2729 Apr 15 20:39 event_watcher.cc
-rw-rw-r-- 1 weizili weizili   996 Apr 15 20:39 event_watcher.h
-rw-rw-r-- 1 weizili weizili  1204 Apr 14 10:55 invoke_timer.cc
-rw-rw-r-- 1 weizili weizili   805 Apr 14 10:55 invoke_timer.h
-rw-rw-r-- 1 weizili weizili   374 Apr 14 10:55 main.cc
$ g++ -std=c++11 event_watcher.cc invoke_timer.cc main.cc -levent
$ ./a.out 
InvokeTimer::InvokeTimer tid=139965845526336 this=0x7ffd2790f780
InvokeTimer::Start tid=139965845526336 this=0x7ffd2790f780
InvokeTimer::Start(AsyncWait) tid=139965845526336 timer=0x14504c0 this=0x7ffd2790f780 timeout(ms)=1000
InvokeTimer::OnTimerTriggered tid=139965845526336 this=0x7ffd2790f780
Print hello world.
InvokeTimer::~InvokeTimer tid=139965845526336 this=0x7ffd2790f780
```

In this implementation, managing the lifecycle of the `InvokeTimer` object is a problem — it requires the caller to manage it themselves.


# 3. Basic Self-Management: basic-02

To achieve self-management of the `InvokeTimer` object's lifecycle, the caller should not need to care about the lifecycle of the `InvokeTimer` object. Imagine this: after the `InvokeTimer` object is created, when the timer expires, we call its bound callback function, and then the `InvokeTimer` object self-destructs. Wouldn't that achieve self-management? Yes, this works. See the code below.

```C++

// Header file

#include <memory>
#include <functional>

struct event_base;

namespace recipes {

class TimerEventWatcher;
class InvokeTimer;

class InvokeTimer {
public:
    typedef std::function<void()> Functor;

    static InvokeTimer* Create(struct event_base* evloop,
                                 double timeout_ms,
                                 const Functor& f);

    ~InvokeTimer();

    void Start();

private:
    InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f);
    void OnTimerTriggered();

private:
    struct event_base* loop_;
    double timeout_ms_;
    Functor functor_;
    std::shared_ptr<TimerEventWatcher> timer_;
};

}


// Implementation file

#include "invoke_timer.h"
#include "event_watcher.h"

#include <thread>
#include <iostream>

namespace recipes {

InvokeTimer::InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f)
    : loop_(evloop), timeout_ms_(timeout_ms), functor_(f) {
    std::cout << "InvokeTimer::InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
}

InvokeTimer* InvokeTimer::Create(struct event_base* evloop, double timeout_ms, const Functor& f) {
    return new InvokeTimer(evloop, timeout_ms, f);
}

InvokeTimer::~InvokeTimer() {
    std::cout << "InvokeTimer::~InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
}

void InvokeTimer::Start() {
    std::cout << "InvokeTimer::Start tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
    timer_.reset(new TimerEventWatcher(loop_, std::bind(&InvokeTimer::OnTimerTriggered, this), timeout_ms_));
    timer_->Init();
    timer_->AsyncWait();
    std::cout << "InvokeTimer::Start(AsyncWait) tid=" << std::this_thread::get_id() << " timer=" << timer_.get() << " this=" << this << " timeout(ms)=" << timeout_ms_ << std::endl;
}

void InvokeTimer::OnTimerTriggered() {
    std::cout << "InvokeTimer::OnTimerTriggered tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
    functor_();
    functor_ = Functor();
    delete this;
}

}

```

Note that in the above implementation, to achieve self-destruction, we must call **delete**, which means the `InvokeTimer` object must be created on the heap. Therefore, we hide its constructor and use a static **Create** member to create instances of the `InvokeTimer` object.

Correspondingly, `main.cc` has also been slightly modified as follows:


```C++
#include "invoke_timer.h"
#include "event_watcher.h"
#include "winmain-inl.h"

#include <event2/event.h>

void Print() {
    std::cout << __FUNCTION__ << " hello world." << std::endl;
}

int main() {
    struct event_base* base = event_base_new();
    auto timer = recipes::InvokeTimer::Create(base, 1000.0, &Print);
    timer->Start(); // Once started, no need to track this object
    event_base_dispatch(base);
    event_base_free(base);
    return 0;
}
```

This implementation does not require the upper-layer caller to manually `delete` the `InvokeTimer` object instance, thus achieving self-management of the `InvokeTimer` object.

Compile and run it. The result meets expectations: when the timer expires, the callback is triggered successfully, and the `InvokeTimer` object is automatically destructed.

```shell
$ ls -l
total 80
-rw-rw-r-- 1 weizili weizili  2729 Apr 15 20:39 event_watcher.cc
-rw-rw-r-- 1 weizili weizili   996 Apr 15 20:39 event_watcher.h
-rw-rw-r-- 1 weizili weizili  1204 Apr 14 10:55 invoke_timer.cc
-rw-rw-r-- 1 weizili weizili   805 Apr 14 10:55 invoke_timer.h
-rw-rw-r-- 1 weizili weizili   374 Apr 14 10:55 main.cc
$ g++ -std=c++11 event_watcher.cc invoke_timer.cc main.cc -levent
$ ./a.out 
InvokeTimer::InvokeTimer tid=139965845526336 this=0x7ffd2790f780
InvokeTimer::Start tid=139965845526336 this=0x7ffd2790f780
InvokeTimer::Start(AsyncWait) tid=139965845526336 timer=0x14504c0 this=0x7ffd2790f780 timeout(ms)=1000
InvokeTimer::OnTimerTriggered tid=139965845526336 this=0x7ffd2790f780
Print hello world.
InvokeTimer::~InvokeTimer tid=139965845526336 this=0x7ffd2790f780
```


# 4. What If You Need to Cancel a Timer: cancel-03

The second implementation above achieved self-management of the timer, where the caller doesn't need to worry about the timer's lifecycle. Then a new requirement came along: the upper-layer caller said, when making a request, you can set a timer to handle timeout, but if the request comes back in time, we need to cancel the timer promptly. How do we handle that?

This means the upper-layer caller must keep a reference to the `InvokeTimer` object instance so that it can cancel the timer early when needed. The upper-layer caller retaining this pointer introduces certain risks, such as misuse — when the `InvokeTimer` object has already auto-destructed, the dangling pointer still exists at the upper-layer caller.

Thus, the smart pointer `shared_ptr` makes its entrance. We want the object that the upper-layer caller sees to exist in the form of `shared_ptr<InvokeTimer>`. Regardless of whether the upper-layer caller retains this `shared_ptr<InvokeTimer>` object, the `InvokeTimer` object should be able to self-manage. In other words, when the upper-layer caller does not retain the `shared_ptr<InvokeTimer>` object, the `InvokeTimer` object must still self-manage.

Here, the `InvokeTimer` object itself must also hold a copy of the `shared_ptr<InvokeTimer>` object. To implement this technique, we need to introduce `enable_shared_from_this`. There is already plenty of material about `enable_shared_from_this` online, so we won't elaborate further here. Let's go straight to the final implementation code:

```C++

// Header file

#include <memory>
#include <functional>

struct event_base;

namespace recipes {

class TimerEventWatcher;
class InvokeTimer;

typedef std::shared_ptr<InvokeTimer> InvokeTimerPtr;

class InvokeTimer : public std::enable_shared_from_this<InvokeTimer> {
public:
    typedef std::function<void()> Functor;

    static InvokeTimerPtr Create(struct event_base* evloop,
                                 double timeout_ms,
                                 const Functor& f);

    ~InvokeTimer();

    void Start();

    void Cancel();

    void set_cancel_callback(const Functor& fn) {
        cancel_callback_ = fn;
    }
private:
    InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f);
    void OnTimerTriggered();
    void OnCanceled();

private:
    struct event_base* loop_;
    double timeout_ms_;
    Functor functor_;
    Functor cancel_callback_;
    std::shared_ptr<TimerEventWatcher> timer_;
    std::shared_ptr<InvokeTimer> self_; // Hold myself
};

}


// Implementation file

#include "invoke_timer.h"
#include "event_watcher.h"

#include <thread>
#include <iostream>

namespace recipes {

InvokeTimer::InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f)
    : loop_(evloop), timeout_ms_(timeout_ms), functor_(f) {
    std::cout << "InvokeTimer::InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
}

InvokeTimerPtr InvokeTimer::Create(struct event_base* evloop, double timeout_ms, const Functor& f) {
    InvokeTimerPtr it(new InvokeTimer(evloop, timeout_ms, f));
    it->self_ = it;
    return it;
}

InvokeTimer::~InvokeTimer() {
    std::cout << "InvokeTimer::~InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
}

void InvokeTimer::Start() {
    std::cout << "InvokeTimer::Start tid=" << std::this_thread::get_id() << " this=" << this << " refcount=" << self_.use_count() << std::endl;
    timer_.reset(new TimerEventWatcher(loop_, std::bind(&InvokeTimer::OnTimerTriggered, shared_from_this()), timeout_ms_));
    timer_->SetCancelCallback(std::bind(&InvokeTimer::OnCanceled, shared_from_this()));
    timer_->Init();
    timer_->AsyncWait();
    std::cout << "InvokeTimer::Start(AsyncWait) tid=" << std::this_thread::get_id() << " timer=" << timer_.get() << " this=" << this << " refcount=" << self_.use_count() << " periodic=" << periodic_ << " timeout(ms)=" << timeout_ms_ << std::endl;
}

void InvokeTimer::Cancel() {
    if (timer_) {
        timer_->Cancel();
    }
}

void InvokeTimer::OnTimerTriggered() {
    std::cout << "InvokeTimer::OnTimerTriggered tid=" << std::this_thread::get_id() << " this=" << this << " use_count=" << self_.use_count() << std::endl;
    functor_();
    functor_ = Functor();
    cancel_callback_ = Functor();
    timer_.reset();
    self_.reset();
}

void InvokeTimer::OnCanceled() {
    std::cout << "InvokeTimer::OnCanceled tid=" << std::this_thread::get_id() << " this=" << this << " use_count=" << self_.use_count() << std::endl;
    if (cancel_callback_) {
        cancel_callback_();
        cancel_callback_ = Functor();
    }
    functor_ = Functor();
    timer_.reset();
    self_.reset();
}

}


```



Correspondingly, `main.cc` has also been slightly modified as follows:


```C++
#include "invoke_timer.h"
#include "event_watcher.h"
#include "winmain-inl.h"

#include <event2/event.h>

void Print() {
    std::cout << __FUNCTION__ << " hello world." << std::endl;
}

int main() {
    struct event_base* base = event_base_new();
    auto timer = recipes::InvokeTimer::Create(base, 1000.0, &Print);
    timer->Start(); // Once started, no need to track this object
    event_base_dispatch(base);
    event_base_free(base);
    return 0;
}
```

This implementation does not require the upper-layer caller to manually `delete` the `InvokeTimer` object instance, thus achieving self-management of the `InvokeTimer` object.

Compile and run it. The result meets expectations: when the timer expires, the callback is triggered successfully, and the `InvokeTimer` object is automatically destructed.




# 5. Implementing a Periodic Timer: periodic-04

The above implementations are all one-shot timer tasks. But what if we want to implement a periodic timer? For example, we have a task that needs to run once per minute.

In fact, based on the third version above, implementing periodic timer functionality is quite easy. You just need to call `timer->AsyncWait()` again in the callback function. The detailed changes are as follows.


Header file invoke_timer.h changes:

```diff

@@ -18,7 +18,8 @@ public:

     static InvokeTimerPtr Create(struct event_base* evloop,
                                  double timeout_ms,
-                                 const Functor& f);
+                                 const Functor& f,
+                                 bool periodic);

     ~InvokeTimer();

@@ -30,7 +31,7 @@ public:
         cancel_callback_ = fn;
     }
 private:
-    InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f);
+    InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f, bool periodic);
     void OnTimerTriggered();
     void OnCanceled();

@@ -40,6 +41,7 @@ private:
     Functor functor_;
     Functor cancel_callback_;
     std::shared_ptr<TimerEventWatcher> timer_;
+    bool periodic_;
     std::shared_ptr<InvokeTimer> self_; // Hold myself
 };
```


Implementation file invoke_timer.cc changes:

```diff

 namespace recipes {

-InvokeTimer::InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f)
-    : loop_(evloop), timeout_ms_(timeout_ms), functor_(f) {
+InvokeTimer::InvokeTimer(struct event_base* evloop, double timeout_ms, const Functor& f, bool periodic)
+    : loop_(evloop), timeout_ms_(timeout_ms), functor_(f), periodic_(periodic) {
     std::cout << "InvokeTimer::InvokeTimer tid=" << std::this_thread::get_id() << " this=" << this << std::endl;
 }

-InvokeTimerPtr InvokeTimer::Create(struct event_base* evloop, double timeout_ms, const Functor& f) {
-    InvokeTimerPtr it(new InvokeTimer(evloop, timeout_ms, f));
+InvokeTimerPtr InvokeTimer::Create(struct event_base* evloop, double timeout_ms, const Functor& f, bool periodic) {
+    InvokeTimerPtr it(new InvokeTimer(evloop, timeout_ms, f, periodic));
     it->self_ = it;
     return it;
 }
@@ -27,7 +27,7 @@ void InvokeTimer::Start() {
     timer_->SetCancelCallback(std::bind(&InvokeTimer::OnCanceled, shared_from_this()));
     timer_->Init();
     timer_->AsyncWait();
 }

 void InvokeTimer::Cancel() {
@@ -39,14 +39,20 @@ void InvokeTimer::Cancel() {
 void InvokeTimer::OnTimerTriggered() {
     std::cout << "InvokeTimer::OnTimerTriggered tid=" << std::this_thread::get_id() << " this=" << this << " use_count=" << self_.use_count() << std::endl;
     functor_();
-    functor_ = Functor();
-    cancel_callback_ = Functor();
-    timer_.reset();
-    self_.reset();
+
+    if (periodic_) {
+        timer_->AsyncWait();
+    } else {
+        functor_ = Functor();
+        cancel_callback_ = Functor();
+        timer_.reset();
+        self_.reset();
+    }
 }

 void InvokeTimer::OnCanceled() {
     std::cout << "InvokeTimer::OnCanceled tid=" << std::this_thread::get_id() << " this=" << this << " use_count=" << self_.use_count() << std::endl;
+    periodic_ = false;
     if (cancel_callback_) {
         cancel_callback_();
         cancel_callback_ = Functor();

```

The main.cc test example has also been modified as follows:


```C++
#include "invoke_timer.h"
#include "event_watcher.h"
#include "winmain-inl.h"

#include <event2/event.h>

void Print() {
    std::cout << __FUNCTION__ << " hello world." << std::endl;
}

int main() {
    struct event_base* base = event_base_new();
    auto timer = recipes::InvokeTimer::Create(base, 1000.0, &Print, true);
    timer->Start();
    timer.reset();
    event_base_dispatch(base);
    event_base_free(base);
    return 0;
}
```

This version is the final implementation. The relevant code is all at [https://github.com/Qihoo360/evpp/tree/master/examples/recipes/self_control_timer]. For ease of demonstration, it has no dependency on [evpp].


# 6. Closing

[evpp] project homepage: [https://github.com/Qihoo360/evpp]
For the detailed code implementation in this article, see [https://github.com/Qihoo360/evpp/tree/master/examples/recipes/self_control_timer]

# 7. evpp Series Article List

[evpp Performance Test (3): A Performance Comparison of Lock-Free Queues boost::lockfree::queue and moodycamel::ConcurrentQueue](http://blog.csdn.net/zieckey/article/details/69803011)
[evpp Performance Test (2): Throughput Comparison with Boost.Asio](http://blog.csdn.net/zieckey/article/details/69170619)
[evpp Performance Test (1): Throughput Comparison with muduo](http://blog.csdn.net/zieckey/article/details/63778715)
[Releasing a High-Performance Reactor-Pattern C++ Network Library: evpp](http://blog.csdn.net/zieckey/article/details/63760757)

[gtest]:https://github.com/google/googletest
[glog]:https://github.com/google/glog
[Golang]:https://golang.org
[muduo]:https://github.com/chenshuo/muduo
[libevent]:https://github.com/libevent/libevent
[libevent2]:https://github.com/libevent/libevent
[LevelDB]:https://github.com/google/leveldb
[rapidjson]:https://github.com/miloyip/
[Boost.Asio]:http://www.boost.org/
[boost.asio]:http://www.boost.org/
[asio]:http://www.boost.org/
[boost]:http://www.boost.org/
[evpp]:https://github.com/Qihoo360/evpp
[https://github.com/Qihoo360/evpp]:https://github.com/Qihoo360/evpp
[evmc]:https://github.com/Qihoo360/evpp/tree/master/apps/evmc
[evnsq]:https://github.com/Qihoo360/evpp/tree/master/apps/evnsq
[https://github.com/Qihoo360/evpp/blob/master/evpp/invoke_timer.h]:https://github.com/Qihoo360/evpp/blob/master/evpp/invoke_timer.h
[https://github.com/Qihoo360/evpp/blob/master/evpp/event_watcher.h]:https://github.com/Qihoo360/evpp/blob/master/evpp/event_watcher.h
[https://github.com/Qihoo360/evpp/blob/master/evpp/event_watcher.cc]:https://github.com/Qihoo360/evpp/blob/master/evpp/event_watcher.cc
[event_watcher.h]:https://github.com/Qihoo360/evpp/blob/master/evpp/event_watcher.h
[event_watcher.cc]:https://github.com/Qihoo360/evpp/blob/master/evpp/event_watcher.cc
[https://github.com/Qihoo360/evpp/tree/master/examples/recipes/self_control_timer]:https://github.com/Qihoo360/evpp/tree/master/examples/recipes/self_control_timer
