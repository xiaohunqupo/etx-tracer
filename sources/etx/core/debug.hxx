#pragma once

#include <stdio.h>
#include <stdint.h>
#include <assert.h>

#if defined(NDEBUG) || defined(_NDEBUG)
#define ETX_DEBUG 0
#else
#define ETX_DEBUG 1
#endif

#if defined(__NVCC__)

#define ETX_NVCC_COMPILER 1
#define ETX_CPU_CODE __host__
#define ETX_GPU_CODE inline __device__
#define ETX_SHARED_CODE ETX_GPU_CODE ETX_CPU_CODE
#define ETX_GPU_DATA __device__
#define ETX_GPU_CALLABLE extern "C" __global__
#define ETX_INIT_WITH(S)

#else

#define ETX_NVCC_COMPILER 0
#define ETX_CPU_CODE
#define ETX_GPU_CODE inline
#define ETX_SHARED_CODE ETX_GPU_CODE ETX_CPU_CODE
#define ETX_GPU_CALLABLE
#define ETX_GPU_DATA
#define ETX_INIT_WITH(S) = S

template <class T>
T atomicAdd(T*, T);

#endif

#if (ETX_NVCC_COMPILER)

#define ETX_DEBUG_BREAK() \
  do {                    \
    __threadfence();      \
    asm("trap;");         \
  } while (0)

#define ETX_ABORT() assert(false)

#else

#define ETX_DEBUG_BREAK() __debugbreak()
#define ETX_ABORT() abort()

#endif

ETX_GPU_CODE void print_invalid_value(const char* name, float v, const char* filename, uint32_t line) {
  printf("Validation failed: %s (%f) at %s [%u]\n", name, v, filename, line);
}

#define ETX_FORCE_ASSERTS 0

#if (ETX_DEBUG || ETX_FORCE_ASSERTS)

ETX_GPU_CODE void printf_assert_info(const char* name_a, const float a, const char* op, const char* name_b, const float b, const char* filename, uint32_t line) {
  printf("Condition failed: (%s:%f) %s (%s:%f) at %s [%u]\n", name_a, a, op, name_b, b, filename, line);
}
ETX_GPU_CODE void printf_assert_info(const char* name_a, const int32_t a, const char* op, const char* name_b, const int32_t b, const char* filename, uint32_t line) {
  printf("Condition failed: (%s:%d) %s (%s:%d) at %s [%u]\n", name_a, a, op, name_b, b, filename, line);
}
ETX_GPU_CODE void printf_assert_info(const char* name_a, const uint32_t a, const char* op, const char* name_b, const uint32_t b, const char* filename, uint32_t line) {
  printf("Condition failed: (%s:%u) %s (%s:%u) at %s [%u]\n", name_a, a, op, name_b, b, filename, line);
}
ETX_GPU_CODE void printf_assert_info(const char* name_a, const int64_t a, const char* op, const char* name_b, const int64_t b, const char* filename, uint32_t line) {
  printf("Condition failed: (%s:%lld) %s (%s:%lld) at %s [%u]\n", name_a, a, op, name_b, b, filename, line);
}
ETX_GPU_CODE void printf_assert_info(const char* name_a, const uint64_t a, const char* op, const char* name_b, const uint64_t b, const char* filename, uint32_t line) {
  printf("Condition failed: (%s:%llu) %s (%s:%llu) at %s [%u]\n", name_a, a, op, name_b, b, filename, line);
}

#if (ETX_NVCC_COMPILER == 0)
inline uint32_t atomicAdd(uint32_t* value, uint32_t add) {
  // TODO : fix that
  return *value++;
}
#endif

#define ETX_ASSERT_SPECIFIC(A, B, OP)                              \
  do {                                                             \
    if (!((A)OP(B))) {                                             \
      static uint32_t reported = 0;                                \
      if (atomicAdd(&reported, 1u) == 0) {                         \
        printf_assert_info(#A, A, #OP, #B, B, __FILE__, __LINE__); \
        ETX_DEBUG_BREAK();                                         \
      }                                                            \
    }                                                              \
  } while (0)

#define ETX_ASSERT_EQUAL(A, B) ETX_ASSERT_SPECIFIC(A, B, ==)
#define ETX_ASSERT_NOT_EQUAL(A, B) ETX_ASSERT_SPECIFIC(A, B, !=)
#define ETX_ASSERT_LESS(A, B) ETX_ASSERT_SPECIFIC(A, B, <)
#define ETX_ASSERT_GREATER(A, B) ETX_ASSERT_SPECIFIC(A, B, >)

#define ETX_ASSERT(condition)                                                       \
  do {                                                                              \
    if (!(condition)) {                                                             \
      static uint32_t reported = 0;                                                 \
      if (atomicAdd(&reported, 1u) == 0) {                                          \
        printf("Condition %s failed at %s [%u]\n", #condition, __FILE__, __LINE__); \
        ETX_DEBUG_BREAK();                                                          \
      }                                                                             \
    }                                                                               \
  } while (0)

#else

#define ETX_ASSERT(condition) \
  do {                        \
  } while (0)

#define ETX_ASSERT_EQUAL(A, B) \
  do {                         \
  } while (0)
#define ETX_ASSERT_NOT_EQUAL(A, B) \
  do {                             \
  } while (0)
#define ETX_ASSERT_LESS(A, B) \
  do {                        \
  } while (0)
#define ETX_ASSERT_GREATER(A, B) \
  do {                           \
  } while (0)

#endif

#define ETX_CRITICAL(condition)                                                            \
  do {                                                                                     \
    if (!(condition)) {                                                                    \
      printf("Critical condition %s failed at %s [%u]\n", #condition, __FILE__, __LINE__); \
      ETX_DEBUG_BREAK();                                                                   \
    }                                                                                      \
  } while (0)

#define ETX_FAIL(msg)                   \
  do {                                  \
    printf("Critical fail: %s\n", msg); \
    ETX_ABORT();                        \
  } while (0)

#define ETX_FAIL_FMT(fmt, ...) \
  do {                         \
    printf(fmt, __VA_ARGS__);  \
    ETX_ABORT();               \
  } while (0)
