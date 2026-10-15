#include "jdebug_priv.h"

#include "syscall.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/ucontext.h>

static struct sigaction g_sigaction_abrt;
static struct sigaction g_sigaction_bus;
static struct sigaction g_sigaction_fpe;
static struct sigaction g_sigaction_ill;
static struct sigaction g_sigaction_segv;
static struct sigaction g_sigaction_sys;

static atomic_flag g_handling_crash = ATOMIC_FLAG_INIT;

void jdebug_crash_handler(siginfo_t* info, void* ucontext);

static void jdebug_handler(int signum, siginfo_t* info, void* ucontext)
{
  // Only one thread may handle a crash at a time.
  // Any other thread crashing concurrently waits here until the process is terminated.
  // (A crash on the same thread while handling cannot reach this point,
  //  since all handled signals are blocked during the handler, see `sa_mask`.)
  if (atomic_flag_test_and_set(&g_handling_crash)) {
    for (;;)
      SYSCALL(SYS_pause);
  }

  signal(signum, SIG_DFL);

  jdebug_crash_handler(info, ucontext);

  struct sigaction* restore_handler = NULL;
  switch (signum) {
    case SIGABRT:
      restore_handler = &g_sigaction_abrt;
      break;
    case SIGBUS:
      restore_handler = &g_sigaction_bus;
      break;
    case SIGFPE:
      restore_handler = &g_sigaction_fpe;
      break;
    case SIGILL:
      restore_handler = &g_sigaction_ill;
      break;
    case SIGSEGV:
      restore_handler = &g_sigaction_segv;
      break;
    case SIGSYS:
      restore_handler = &g_sigaction_sys;
      break;
    default:
      break;
  }

  if (restore_handler) {
    sigaction(signum, restore_handler, NULL);
  }

  atomic_flag_clear(&g_handling_crash);

  raise(signum);
}

void jdebug_install_signal_handlers(void)
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_flags     = SA_SIGINFO;
  sa.sa_sigaction = jdebug_handler;
  sigemptyset(&sa.sa_mask);
  sigaddset(&sa.sa_mask, SIGABRT);
  sigaddset(&sa.sa_mask, SIGBUS);
  sigaddset(&sa.sa_mask, SIGFPE);
  sigaddset(&sa.sa_mask, SIGILL);
  sigaddset(&sa.sa_mask, SIGSEGV);
  sigaddset(&sa.sa_mask, SIGSYS);
  sigaction(SIGABRT, &sa, &g_sigaction_abrt);
  sigaction(SIGBUS, &sa, &g_sigaction_bus);
  sigaction(SIGFPE, &sa, &g_sigaction_fpe);
  sigaction(SIGILL, &sa, &g_sigaction_ill);
  sigaction(SIGSEGV, &sa, &g_sigaction_segv);
  sigaction(SIGSYS, &sa, &g_sigaction_sys);
}

void jdebug_uninstall_signal_handlers(void)
{
  sigaction(SIGABRT, &g_sigaction_abrt, NULL);
  sigaction(SIGBUS, &g_sigaction_bus, NULL);
  sigaction(SIGFPE, &g_sigaction_fpe, NULL);
  sigaction(SIGILL, &g_sigaction_ill, NULL);
  sigaction(SIGSEGV, &g_sigaction_segv, NULL);
  sigaction(SIGSYS, &g_sigaction_sys, NULL);
}

static void print_sigbus(const logger_t* log, const siginfo_t* info)
{
  switch (info->si_code) {
    case BUS_ADRALN:
      jdebug_log_format(log, "reason: invalid address alignment. ");
      break;
    case BUS_ADRERR:
      jdebug_log_format(log, "reason: nonexistent physical address. ");
      break;
    case BUS_OBJERR:
      jdebug_log_format(log, "reason: object-specific hardware error. ");
      break;
    case BUS_MCEERR_AR:
      jdebug_log_format(log, "reason: hardware memory error consumed on a machine check; action required. ");
      break;
    case BUS_MCEERR_AO:
      jdebug_log_format(log,
                        "reason: hardware memory error detected in process but not consumed; action optional. ");
      break;
    default:
      jdebug_log_format(log, "reason: unspecified. ");
      break;
  }
  jdebug_log_format(log, "fault address: 0x%p\n", info->si_addr);
}

static void print_sigfpe(const logger_t* log, const siginfo_t* info)
{
  switch (info->si_code) {
    case FPE_INTDIV:
      jdebug_log_format(log, "reason: integer divide by zero. ");
      break;
    case FPE_INTOVF:
      jdebug_log_format(log, "reason: integer overflow. ");
      break;
    case FPE_FLTDIV:
      jdebug_log_format(log, "reason: floating-point divide by zero. ");
      break;
    case FPE_FLTOVF:
      jdebug_log_format(log, "reason: floating-point overflow. ");
      break;
    case FPE_FLTUND:
      jdebug_log_format(log, "reason: floating-point underflow. ");
      break;
    case FPE_FLTRES:
      jdebug_log_format(log, "reason: floating-point inexact result. ");
      break;
    case FPE_FLTINV:
      jdebug_log_format(log, "reason: floating-point invalid operation. ");
      break;
    case FPE_FLTSUB:
      jdebug_log_format(log, "reason: subscript out of range. ");
      break;
    default:
      jdebug_log_format(log, "reason: unspecified. ");
      break;
  }
  jdebug_log_format(log, "fault address: 0x%p\n", info->si_addr);
}

static void print_sigill(const logger_t* log, const siginfo_t* info)
{
  switch (info->si_code) {
    case ILL_ILLOPC:
      jdebug_log_format(log, "reason: illegal opcode. ");
      break;
    case ILL_ILLOPN:
      jdebug_log_format(log, "reason: illegal operand. ");
      break;
    case ILL_ILLADR:
      jdebug_log_format(log, "reason: illegal addressing mode. ");
      break;
    case ILL_ILLTRP:
      jdebug_log_format(log, "reason: illegal trap. ");
      break;
    case ILL_PRVOPC:
      jdebug_log_format(log, "reason: privileged opcode. ");
      break;
    case ILL_PRVREG:
      jdebug_log_format(log, "reason: privileged register. ");
      break;
    case ILL_COPROC:
      jdebug_log_format(log, "reason: coprocessor error. ");
      break;
    case ILL_BADSTK:
      jdebug_log_format(log, "reason: internal stack error. ");
      break;
    default:
      jdebug_log_format(log, "reason: unspecified. ");
      break;
  }
  jdebug_log_format(log, "fault address: 0x%p\n", info->si_addr);
}

static void print_sigsegv(const logger_t* log, const siginfo_t* info)
{
  switch (info->si_code) {
    case SEGV_MAPERR:
      jdebug_log_format(log, "reason: address not mapped to object. ");
      break;
    case SEGV_ACCERR:
      jdebug_log_format(log, "reason: invalid permissions for mapped object. ");
      break;
    case SEGV_BNDERR:
      jdebug_log_format(log, "reason: failed address bound checks. ");
      break;
    case SEGV_PKUERR:
      jdebug_log_format(log, "reason: access was denied by memory protection keys. ");
      break;
    default:
      jdebug_log_format(log, "reason: unspecified. ");
      break;
  }
  jdebug_log_format(log, "fault address: 0x%p\n", info->si_addr);
}

static void print_ucontext(const logger_t* log, const ucontext_t* ctx)
{
#ifdef __x86_64__
  jdebug_log_format(log, "registers:\n");
  jdebug_log_format(log, "  rax: 0x%016lX rbx: 0x%016lX rcx: 0x%016lX rdx: 0x%016lX\n",
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RAX],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RBX],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RCX],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RDX]);
  jdebug_log_format(log, "  rdi: 0x%016lX rsi: 0x%016lX rbp: 0x%016lX rsp: 0x%016lX\n",
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RDI],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RSI],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RBP],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RSP]);
  jdebug_log_format(log, "   r8: 0x%016lX  r9: 0x%016lX r10: 0x%016lX r11: 0x%016lX\n",
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R8],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R9],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R10],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R11]);
  jdebug_log_format(log, "  r12: 0x%016lX r13: 0x%016lX r14: 0x%016lX r15: 0x%016lX\n",
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R12],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R13],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R14],
                    (uint64_t) ctx->uc_mcontext.gregs[REG_R15]);
  const uint64_t ssel = ctx->uc_mcontext.gregs[REG_CSGSFS];
  jdebug_log_format(log, "  rip: 0x%016lX  cs: 0x%04X  gs: 0x%04X  fs: 0x%04X\n",
                    (uint64_t) ctx->uc_mcontext.gregs[REG_RIP],
                    (uint32_t) (ssel & 0xFFFF),
                    (uint32_t) ((ssel >> 16) & 0xFFFF),
                    (uint32_t) ((ssel >> 32) & 0xFFFF));
  const uint64_t eflags = ctx->uc_mcontext.gregs[REG_EFL];
  jdebug_log_format(log, "  eflags: 0x%lX [", eflags);
  if (eflags & (1 << 0))
    jdebug_log_format(log, " CF");
  if (eflags & (1 << 2))
    jdebug_log_format(log, " PF");
  if (eflags & (1 << 4))
    jdebug_log_format(log, " AF");
  if (eflags & (1 << 6))
    jdebug_log_format(log, " ZF");
  if (eflags & (1 << 7))
    jdebug_log_format(log, " SF");
  if (eflags & (1 << 8))
    jdebug_log_format(log, " TF");
  if (eflags & (1 << 9))
    jdebug_log_format(log, " IF");
  if (eflags & (1 << 10))
    jdebug_log_format(log, " DF");
  if (eflags & (1 << 11))
    jdebug_log_format(log, " OF");
  jdebug_log_format(log, " ]\n");
#else
#error "unsupported architecture"
#endif
}

void jdebug_print_cpp_exception(const logger_t* log);

// `strsignal` is not async-signal-safe
static const char* jdebug_signal_name(int signo)
{
  switch (signo) {
    case SIGABRT:
      return "Aborted";
    case SIGBUS:
      return "Bus error";
    case SIGFPE:
      return "Floating point exception";
    case SIGILL:
      return "Illegal instruction";
    case SIGSEGV:
      return "Segmentation fault";
    case SIGSYS:
      return "Bad system call";
    default:
      return "Unknown signal";
  }
}

void jdebug_print_signal_info(const logger_t* log, const siginfo_t* info, const void* ucontext)
{
  jdebug_log_format(log, "fatal signal occurred: %s (%d)\n",
                    jdebug_signal_name(info->si_signo),
                    info->si_signo);

  switch (info->si_signo) {
    default:
    case SIGSYS:
      /* nothing additional to log */
      break;
    case SIGABRT:
#ifdef JDEBUG_PRINT_EXCEPTION
      jdebug_print_cpp_exception(log);
#else
      jdebug_log_format(log, "abort called (possibly due to an uncaught C++ exception)\n");
#endif
      break;
    case SIGBUS:
      print_sigbus(log, info);
      break;
    case SIGFPE:
      print_sigfpe(log, info);
      break;
    case SIGILL:
      print_sigill(log, info);
      break;
    case SIGSEGV:
      print_sigsegv(log, info);
      break;
  }

  print_ucontext(log, ucontext);
}
