# Android NDK workaround: pthread is built into bionic libc
# No separate pthread library exists on Android
# This file provides the FindThreads result for Android cross-compilation

if(ANDROID)
    set(CMAKE_HAVE_THREADS_LIBRARY ON)
    set(CMAKE_USE_PTHREADS_INIT ON)
    set(CMAKE_THREAD_LIBS_INIT "")
    set(Threads_FOUND TRUE)
    set(CMAKE_HAVE_PTHREAD_H ON)
    set(CMAKE_HAVE_PTHREADS_CREATE ON)
    set(CMAKE_HAVE_PTHREAD_CREATE ON)
endif()
