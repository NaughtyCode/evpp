#pragma once

// Cross-platform shared library symbol visibility.
//
// When building the engine library, define ENGINE_BUILD so symbols are
// exported. Consumers (who do not define ENGINE_BUILD) will see dllimport
// on Windows; on other platforms the attribute is a no-op when importing.

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef ENGINE_BUILD
    #define ENGINE_API __declspec(dllexport)
  #else
    #define ENGINE_API __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define ENGINE_API __attribute__((visibility("default")))
  #else
    #define ENGINE_API
  #endif
#endif
