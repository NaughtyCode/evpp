
1. posttask1: multiple threads arranged in a circle, each posting a task to the next thread. The task increments a member variable until a set count is reached.
1. posttask2: an even number of threads paired into groups. Within each group, two threads post tasks back and forth, incrementing a member variable (requires atomic) until a set count is reached.
1. posttask3: thread 1 sends a specified number of tasks to thread 2.
1. posttask4: thread 1 sends a specified number of tasks to thread 2, but doesn't actually send that many times. Instead, it checks a locked queue — if the queue is not empty, it inserts directly without sending.
1. posttask5: an improved version of posttask4. The queue stores the tasks themselves. This is closer to real-world scenarios. posttask4 oversimplified the task.
1. posttask6: multiple threads simultaneously post tasks to the same thread, incrementing a member variable until a set count is reached. In a multi-producer, single-consumer scenario, using boost::lockfree yields about twice the performance of std::mutex. boost::lockfree is recommended.

[huyuguang@dtrans1 ~/code/asio]$ ./asio_test.exe posttask3 10000000 use time(us): 9077386

[huyuguang@dtrans1 ~/code/asio]$ ./asio_test.exe posttask4 10000000 use time(us): 638762

[huyuguang@dtrans1 ~/code/asio]$ ./asio_test.exe posttask5 10000000 use time(us): 4202412

posttask4 represents the ideal case. posttask5 is closer to real-world scenarios. However, since posttask5's implementation is highly optimized, including using two pre-reserved vectors that swap back and forth, I doubt whether such upper-layer optimizations like posttask5 are worth adopting.
