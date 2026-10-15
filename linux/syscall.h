#pragma once

#include <errno.h>
#include <sys/syscall.h>

#define __SYSCALL_TYPE1(__x) __typeof__((__x) - (__x))
#define __SYSCALL_ARG(__x) ((__SYSCALL_TYPE1(__x))(__x))
#define __SYSCALL_TYPE(__x) __typeof__(__SYSCALL_ARG(__x))
#define __SYSCALL_ARGS(__x, __n) __SYSCALL_TYPE(__x) __n = __SYSCALL_ARG(__x)
#define __SYSCALL_REG(__x, __n, __r) register __SYSCALL_TYPE(__x) __n asm(#__r)

#define __SYSCALL0(__number)          \
  __extension__({                     \
    long __r;                         \
    asm volatile(                     \
      "syscall\n"                     \
      : "=a"(__r)                     \
      : "0"(__number)                 \
      : "memory", "cc", "r11", "cx"); \
    __r;                              \
  })

#define __SYSCALL1(__number, arg0)           \
  __extension__({                            \
    long __r;                                \
    __SYSCALL_ARGS(arg0, __arg0);            \
    __SYSCALL_REG(arg0, __a0, rdi) = __arg0; \
    asm volatile(                            \
      "syscall\n"                            \
      : "=a"(__r)                            \
      : "0"(__number), "r"(__a0)             \
      : "memory", "cc", "r11", "cx");        \
    __r;                                     \
  })

#define __SYSCALL2(__number, arg0, arg1)     \
  __extension__({                            \
    long __r;                                \
    __SYSCALL_ARGS(arg1, __arg1);            \
    __SYSCALL_REG(arg1, __a1, rsi) = __arg1; \
    __SYSCALL_ARGS(arg0, __arg0);            \
    __SYSCALL_REG(arg0, __a0, rdi) = __arg0; \
    asm volatile(                            \
      "syscall\n"                            \
      : "=a"(__r)                            \
      : "0"(__number), "r"(__a0), "r"(__a1)  \
      : "memory", "cc", "r11", "cx");        \
    __r;                                     \
  })

#define __SYSCALL3(__number, arg0, arg1, arg2)         \
  __extension__({                                      \
    long __r;                                          \
    __SYSCALL_ARGS(arg2, __arg2);                      \
    __SYSCALL_REG(arg2, __a2, rdx) = __arg2;           \
    __SYSCALL_ARGS(arg1, __arg1);                      \
    __SYSCALL_REG(arg1, __a1, rsi) = __arg1;           \
    __SYSCALL_ARGS(arg0, __arg0);                      \
    __SYSCALL_REG(arg0, __a0, rdi) = __arg0;           \
    asm volatile(                                      \
      "syscall\n"                                      \
      : "=a"(__r)                                      \
      : "0"(__number), "r"(__a0), "r"(__a1), "r"(__a2) \
      : "memory", "cc", "r11", "cx");                  \
    __r;                                               \
  })

#define __SYSCALL4(__number, arg0, arg1, arg2, arg3)              \
  __extension__({                                                 \
    long __r;                                                     \
    __SYSCALL_ARGS(arg3, __arg3);                                 \
    __SYSCALL_REG(arg3, __a3, r10) = __arg3;                      \
    __SYSCALL_ARGS(arg2, __arg2);                                 \
    __SYSCALL_REG(arg2, __a2, rdx) = __arg2;                      \
    __SYSCALL_ARGS(arg1, __arg1);                                 \
    __SYSCALL_REG(arg1, __a1, rsi) = __arg1;                      \
    __SYSCALL_ARGS(arg0, __arg0);                                 \
    __SYSCALL_REG(arg0, __a0, rdi) = __arg0;                      \
    asm volatile(                                                 \
      "syscall\n"                                                 \
      : "=a"(__r)                                                 \
      : "0"(__number), "r"(__a0), "r"(__a1), "r"(__a2), "r"(__a3) \
      : "memory", "cc", "r11", "cx");                             \
    __r;                                                          \
  })

#define __SYSCALL5(__number, arg0, arg1, arg2, arg3, arg4)                   \
  __extension__({                                                            \
    long __r;                                                                \
    __SYSCALL_ARGS(arg4, __arg4);                                            \
    __SYSCALL_REG(arg4, __a4, r8) = __arg4;                                  \
    __SYSCALL_ARGS(arg3, __arg3);                                            \
    __SYSCALL_REG(arg3, __a3, r10) = __arg3;                                 \
    __SYSCALL_ARGS(arg2, __arg2);                                            \
    __SYSCALL_REG(arg2, __a2, rdx) = __arg2;                                 \
    __SYSCALL_ARGS(arg1, __arg1);                                            \
    __SYSCALL_REG(arg1, __a1, rsi) = __arg1;                                 \
    __SYSCALL_ARGS(arg0, __arg0);                                            \
    __SYSCALL_REG(arg0, __a0, rdi) = __arg0;                                 \
    asm volatile(                                                            \
      "syscall\n"                                                            \
      : "=a"(__r)                                                            \
      : "0"(__number), "r"(__a0), "r"(__a1), "r"(__a2), "r"(__a3), "r"(__a4) \
      : "memory", "cc", "r11", "cx");                                        \
    __r;                                                                     \
  })

#define __SYSCALL6(__number, arg0, arg1, arg2, arg3, arg4, arg5)                        \
  __extension__({                                                                       \
    long __r;                                                                           \
    __SYSCALL_ARGS(arg5, __arg5);                                                       \
    __SYSCALL_REG(arg5, __a5, r9) = __arg5;                                             \
    __SYSCALL_ARGS(arg4, __arg4);                                                       \
    __SYSCALL_REG(arg4, __a4, r8) = __arg4;                                             \
    __SYSCALL_ARGS(arg3, __arg3);                                                       \
    __SYSCALL_REG(arg3, __a3, r10) = __arg3;                                            \
    __SYSCALL_ARGS(arg2, __arg2);                                                       \
    __SYSCALL_REG(arg2, __a2, rdx) = __arg2;                                            \
    __SYSCALL_ARGS(arg1, __arg1);                                                       \
    __SYSCALL_REG(arg1, __a1, rsi) = __arg1;                                            \
    __SYSCALL_ARGS(arg0, __arg0);                                                       \
    __SYSCALL_REG(arg0, __a0, rdi) = __arg0;                                            \
    asm volatile(                                                                       \
      "syscall\n"                                                                       \
      : "=a"(__r)                                                                       \
      : "0"(__number), "r"(__a0), "r"(__a1), "r"(__a2), "r"(__a3), "r"(__a4), "r"(__a5) \
      : "memory", "cc", "r11", "cx");                                                   \
    __r;                                                                                \
  })

#define __SYSCALL_ARG_N(_0, _1, _2, _3, _4, _5, _6, _7, _N, ...) _N
#define __SYSCALL_NARG_(...) __SYSCALL_ARG_N(__VA_ARGS__, 7, 6, 5, 4, 3, 2, 1, 0, )
#define __SYSCALL_NARG(...) __SYSCALL_NARG_(__VA_ARGS__)
#define __SYSCALL_CAT(__a, __b) __SYSCALL_CAT_I(__a, __b)
#define __SYSCALL_CAT_I(__a, __b) __a##__b
#define __SYSCALL_OVERLOAD(__base, ...) __SYSCALL_CAT(__base, __SYSCALL_NARG(__VA_ARGS__))(__VA_ARGS__)

#define SYSCALL(...) __SYSCALL_OVERLOAD(__SYSCALL, __VA_ARGS__)

#define SYSCLONE(fn, child_stack, flags, arg, parent_tidptr, newtls, child_tidptr) \
  __extension__({                                                                  \
    long __r;                                                                      \
    void* __stack                       = (void*) (child_stack);                   \
    register void* __tls __asm__("r8")  = newtls;                                  \
    register int* __ctid __asm__("r10") = child_tidptr;                            \
    asm volatile(                                                                  \
      "testq  %[clone_fn],%[clone_fn]\n"                                           \
      "jz     1f\n"                                                                \
                                                                                   \
      "testq  %[clone_stack],%[clone_stack]\n"                                     \
      "jz     1f\n"                                                                \
                                                                                   \
      "subq   $16,%[clone_stack]\n"                                                \
                                                                                   \
      "movq   %[clone_arg],8(%[clone_stack])\n"                                    \
      "movq   %[clone_fn],0(%[clone_stack])\n"                                     \
                                                                                   \
      "movq   %[nr_clone],%%rax\n"                                                 \
      "syscall\n"                                                                  \
                                                                                   \
      "testq  %%rax,%%rax\n"                                                       \
      "jnz    1f\n"                                                                \
                                                                                   \
      "xorq   %%rbp,%%rbp\n"                                                       \
                                                                                   \
      "popq   %%rax\n"                                                             \
      "popq   %%rdi\n"                                                             \
      "call   *%%rax\n"                                                            \
                                                                                   \
      "movq   %%rax,%%rdi\n"                                                       \
      "movq   %[nr_exit],%%rax\n"                                                  \
      "syscall\n"                                                                  \
                                                                                   \
      "1:\n"                                                                       \
      : "=a"(__r), [clone_stack] "+S"(__stack)                                     \
      : "0"(-EINVAL), [nr_clone] "i"(__NR_clone), [nr_exit] "i"(__NR_exit),        \
        [clone_fn] "r"(fn), "D"((unsigned long) (flags)), [clone_arg] "r"(arg),    \
        "d"(parent_tidptr), "r"(__tls), "r"(__ctid)                                \
      : "memory", "r11", "rcx");                                                   \
    __r;                                                                           \
  })
