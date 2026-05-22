# Preload script for Android cross-compilation
# Set before CMake configuration to work around pthread detection issues
# On Android, pthread is built into bionic libc - no separate library needed

set(CMAKE_HAVE_THREADS_LIBRARY ON CACHE BOOL "Threads available (Android bionic)")
set(CMAKE_USE_PTHREADS_INIT ON CACHE BOOL "Use pthreads (Android bionic)")
set(CMAKE_THREAD_LIBS_INIT "" CACHE STRING "No separate thread library needed on Android")
set(CMAKE_HAVE_PTHREAD_H ON CACHE BOOL "pthread.h available")
