#include <jdebug.h>

#include "log.h"
#include "symbol.h"

#include <windows.h>

#include <DbgHelp.h>
#include <Psapi.h>
#include <tlhelp32.h>

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>

#ifdef _MSC_VER
#define JDEBUG_THREAD_LOCAL __declspec(thread)
#else
#define JDEBUG_THREAD_LOCAL __thread
#endif

#define JDEBUG_CXX_EXCEPTION_CODE_MSVC 0xE06D7363
#define JDEBUG_CXX_EXCEPTION_CODE_GCC 0x20474343

static wchar_t g_log_filename[MAX_PATH]  = {0};
static wchar_t g_core_filename[MAX_PATH] = {0};
static int g_use_temp_log_filename       = 0;
static _Atomic int g_init_count          = 0;
static volatile LONG g_handling_crash    = 0;

static HANDLE g_first_exception_filter_hdl                  = NULL;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prev_exception_filter = NULL;
static void(__cdecl* g_prev_sigabrt)(int)                   = SIG_IGN;
static unsigned int g_prev_abort_behavior                   = 0;
#define JDEBUG_ABORT_BEHAVIOUR_MASK (_WRITE_ABORT_MSG | _CALL_REPORTFAULT)

static void jdebug_abort_handler(int signo);
static LONG WINAPI jdebug_exception_filter(LPEXCEPTION_POINTERS exception_info);
static LONG WINAPI jdebug_exception_filter_first(LPEXCEPTION_POINTERS exception_info);

static void jdebug_set_temp_log_filename(void)
{
  wchar_t temp_path[MAX_PATH + 1];
  GetTempPathW(MAX_PATH + 1, temp_path);
  GetTempFileNameW(temp_path, L"stack", 0, g_log_filename);
}

void jdebug_crash_handler_early(int argc, path_char_t* argv[])
{
  (void) argc;
  (void) argv;
}

void jdebug_crash_handler_init(jdebug_crash_handler_init_info_t* init_info_argument)
{
  if (atomic_fetch_add(&g_init_count, 1) == 0) {

    jdebug_crash_handler_init_info_t init_info_default;
    jdebug_crash_handler_init_info_t* init_info;

    if (init_info_argument) {
      init_info = init_info_argument;
    } else {
      jdebug_crash_handler_init_info_reset(&init_info_default);
      init_info = &init_info_default;
    }

    if (NULL != init_info->log_filename) {
      g_use_temp_log_filename = 0;
      if (!_wfullpath(g_log_filename, init_info->log_filename, MAX_PATH))
        ZeroMemory(g_log_filename, sizeof(g_log_filename));
    } else {
      // the temporary file is only created when a crash occurs
      g_use_temp_log_filename = 1;
      ZeroMemory(g_log_filename, sizeof(g_log_filename));
    }
    if (NULL != init_info->coredump_filename) {
      if (!_wfullpath(g_core_filename, init_info->coredump_filename, MAX_PATH))
        ZeroMemory(g_core_filename, sizeof(g_core_filename));
    } else {
      ZeroMemory(g_core_filename, sizeof(g_core_filename));
    }

    g_first_exception_filter_hdl = AddVectoredExceptionHandler(1, jdebug_exception_filter_first);
    g_prev_exception_filter      = SetUnhandledExceptionFilter(jdebug_exception_filter);
    g_prev_sigabrt               = signal(SIGABRT, jdebug_abort_handler);
    g_prev_abort_behavior        = _set_abort_behavior(0, JDEBUG_ABORT_BEHAVIOUR_MASK);
  }
}

void jdebug_crash_handler_deinit(void)
{
  const int val = atomic_fetch_sub(&g_init_count, 1);
  if (val == 1) {
    _set_abort_behavior(g_prev_abort_behavior, JDEBUG_ABORT_BEHAVIOUR_MASK);
    g_prev_abort_behavior = 0;

    signal(SIGABRT, g_prev_sigabrt);
    g_prev_sigabrt = SIG_IGN;

    SetUnhandledExceptionFilter(g_prev_exception_filter);
    g_prev_exception_filter = NULL;

    RemoveVectoredExceptionHandler(g_first_exception_filter_hdl);
    g_first_exception_filter_hdl = NULL;

    ZeroMemory(g_log_filename, sizeof(g_log_filename));
    ZeroMemory(g_core_filename, sizeof(g_core_filename));
    g_use_temp_log_filename = 0;
  } else if (val <= 0) {
    fprintf(stderr, "jdebug_crash_handler_deinit called too many times\n");
    abort();
  }
}

#ifdef JDEBUG_PRINT_EXCEPTION
void jdebug_print_cpp_exception(const logger_t* log, DWORD exception_code, ULONG_PTR* exception_information);
#endif

static DWORD jdebug_print_exception(logger_t* log, LPEXCEPTION_POINTERS exception_info)
{
  const DWORD code = exception_info->ExceptionRecord->ExceptionCode;
  DWORD exit_code  = EXIT_FAILURE;

  jdebug_log_printf(log, "fatal exception occurred: ");

  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      jdebug_log_printf(log, "Access Violation");
      exit_code = STATUS_ACCESS_VIOLATION;
      break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      jdebug_log_printf(log, "Array Bounds Exceeded");
      exit_code = STATUS_ARRAY_BOUNDS_EXCEEDED;
      break;
    case EXCEPTION_BREAKPOINT:
      jdebug_log_printf(log, "Breakpoint");
      exit_code = STATUS_BREAKPOINT;
      break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      jdebug_log_printf(log, "Datatype Misalignment");
      exit_code = STATUS_DATATYPE_MISALIGNMENT;
      break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
      jdebug_log_printf(log, "FLT Denormal Operand");
      exit_code = STATUS_FLOAT_DENORMAL_OPERAND;
      break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      jdebug_log_printf(log, "FLT Divide By Zero");
      exit_code = STATUS_FLOAT_DIVIDE_BY_ZERO;
      break;
    case EXCEPTION_FLT_INEXACT_RESULT:
      jdebug_log_printf(log, "FLT Inexact Result");
      exit_code = STATUS_FLOAT_INEXACT_RESULT;
      break;
    case EXCEPTION_FLT_INVALID_OPERATION:
      jdebug_log_printf(log, "FLT Invalid Operation");
      exit_code = STATUS_FLOAT_INVALID_OPERATION;
      break;
    case EXCEPTION_FLT_OVERFLOW:
      jdebug_log_printf(log, "FLT Overflow");
      exit_code = STATUS_FLOAT_OVERFLOW;
      break;
    case EXCEPTION_FLT_STACK_CHECK:
      jdebug_log_printf(log, "FLT Stack Check");
      exit_code = STATUS_FLOAT_STACK_CHECK;
      break;
    case EXCEPTION_FLT_UNDERFLOW:
      jdebug_log_printf(log, "FLT Underflow");
      exit_code = STATUS_FLOAT_UNDERFLOW;
      break;
    case EXCEPTION_GUARD_PAGE:
      jdebug_log_printf(log, "Guard Page");
      exit_code = STATUS_GUARD_PAGE_VIOLATION;
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      jdebug_log_printf(log, "Illegal Instruction");
      exit_code = STATUS_ILLEGAL_INSTRUCTION;
      break;
    case EXCEPTION_IN_PAGE_ERROR:
      jdebug_log_printf(log, "In Page Error");
      exit_code = STATUS_IN_PAGE_ERROR;
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      jdebug_log_printf(log, "INT Divide By Zero");
      exit_code = STATUS_INTEGER_DIVIDE_BY_ZERO;
      break;
    case EXCEPTION_INT_OVERFLOW:
      jdebug_log_printf(log, "INT Overflow");
      exit_code = STATUS_INTEGER_OVERFLOW;
      break;
    case EXCEPTION_INVALID_DISPOSITION:
      jdebug_log_printf(log, "Invalid Disposition");
      exit_code = STATUS_INVALID_DISPOSITION;
      break;
    case EXCEPTION_INVALID_HANDLE:
      jdebug_log_printf(log, "Invalid Handle");
      exit_code = STATUS_INVALID_HANDLE;
      break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      jdebug_log_printf(log, "Noncontinuable Exception");
      exit_code = STATUS_NONCONTINUABLE_EXCEPTION;
      break;
    case EXCEPTION_PRIV_INSTRUCTION:
      jdebug_log_printf(log, "Priv Instruction");
      exit_code = STATUS_PRIVILEGED_INSTRUCTION;
      break;
    case EXCEPTION_SINGLE_STEP:
      jdebug_log_printf(log, "Single Step");
      exit_code = STATUS_SINGLE_STEP;
      break;
    case EXCEPTION_STACK_OVERFLOW:
      jdebug_log_printf(log, "Stack Overflow");
      exit_code = STATUS_STACK_OVERFLOW;
      break;
    case STATUS_UNWIND_CONSOLIDATE:
      jdebug_log_printf(log, "Unwind Consolidate");
      exit_code = STATUS_UNWIND_CONSOLIDATE;
      break;
    case JDEBUG_CXX_EXCEPTION_CODE_MSVC:
    case JDEBUG_CXX_EXCEPTION_CODE_GCC:
#ifdef JDEBUG_PRINT_EXCEPTION
      jdebug_log_printf(log, "Unhandled C++ Exception");
      jdebug_print_cpp_exception(log, exception_info->ExceptionRecord->ExceptionCode, exception_info->ExceptionRecord->ExceptionInformation);
#else
      jdebug_log_printf(log, "Unhandled C++ Exception\n");
#endif
      exit_code = STATUS_NONCONTINUABLE_EXCEPTION;
      break;
    default:
      jdebug_log_printf(log, "Unknown Exception (0x%08lX)", exception_info->ExceptionRecord->ExceptionCode);
      break;
  }

  jdebug_log_printf(log, "\n");
  return exit_code;
}

static void jdebug_print_context(const logger_t* log, LPCONTEXT ctx)
{
#ifdef _M_X64
  jdebug_log_printf(log, "registers:\n");
  jdebug_log_printf(log, "  rax: 0x%016llX rbx: 0x%016llX rcx: 0x%016llX rdx: 0x%016llX\n",
                    ctx->Rax,
                    ctx->Rbx,
                    ctx->Rcx,
                    ctx->Rdx);
  jdebug_log_printf(log, "  rdi: 0x%016llX rsi: 0x%016llX rbp: 0x%016llX rsp: 0x%016llX\n",
                    ctx->Rdi,
                    ctx->Rsi,
                    ctx->Rbp,
                    ctx->Rsp);
  jdebug_log_printf(log, "   r8: 0x%016llX  r9: 0x%016llX r10: 0x%016llX r11: 0x%016llX\n",
                    ctx->R8,
                    ctx->R9,
                    ctx->R10,
                    ctx->R11);
  jdebug_log_printf(log, "  r12: 0x%016llX r13: 0x%016llX r14: 0x%016llX r15: 0x%016llX\n",
                    ctx->R12,
                    ctx->R13,
                    ctx->R14,
                    ctx->R15);
  jdebug_log_printf(log, "  rip: 0x%016llX  cs: 0x%04x  gs: 0x%04x  fs: 0x%04x\n",
                    ctx->Rip,
                    ctx->SegCs,
                    ctx->SegGs,
                    ctx->SegFs);
  size_t eflags = ctx->EFlags;
  jdebug_log_printf(log, "  eflags: 0x%llX [", eflags);
  if (eflags & (1 << 0))
    jdebug_log_printf(log, " CF");
  if (eflags & (1 << 2))
    jdebug_log_printf(log, " PF");
  if (eflags & (1 << 4))
    jdebug_log_printf(log, " AF");
  if (eflags & (1 << 6))
    jdebug_log_printf(log, " ZF");
  if (eflags & (1 << 7))
    jdebug_log_printf(log, " SF");
  if (eflags & (1 << 8))
    jdebug_log_printf(log, " TF");
  if (eflags & (1 << 9))
    jdebug_log_printf(log, " IF");
  if (eflags & (1 << 10))
    jdebug_log_printf(log, " DF");
  if (eflags & (1 << 11))
    jdebug_log_printf(log, " OF");
  jdebug_log_printf(log, " ]\n");
#else
#error "unsupported architecture"
#endif
}

static void jdebug_init_symbol_handler(void)
{
  SymInitialize(GetCurrentProcess(), NULL, FALSE);
  DWORD options = SymGetOptions();
  options |= SYMOPT_LOAD_LINES | SYMOPT_UNDNAME;
  SymSetOptions(options);
}

static void jdebug_print_modules(logger_t* log)
{
  HANDLE process     = GetCurrentProcess();
  DWORD bytes_needed = 0;
  EnumProcessModules(process, NULL, 0, &bytes_needed);

  HMODULE* modules = malloc(bytes_needed);
  if (!modules)
    return;

  // the module list may have grown since the size query, only use what fits
  DWORD buffer_size = bytes_needed;
  if (!EnumProcessModules(process, modules, buffer_size, &bytes_needed)) {
    free(modules);
    return;
  }
  if (bytes_needed > buffer_size)
    bytes_needed = buffer_size;

  jdebug_log_printf(log, "loaded modules:\n");

  DWORD module_count = bytes_needed / sizeof(HMODULE);
  for (DWORD i = 0; i < module_count; i++) {
    MODULEINFO module_info       = {0};
    char file_name[MAX_PATH + 1] = {0};
    char base_name[MAX_PATH + 1] = {0};
    if (!GetModuleInformation(process, modules[i], &module_info, sizeof(module_info)))
      continue;
    GetModuleFileNameExA(process, modules[i], file_name, sizeof(file_name));
    GetModuleBaseNameA(process, modules[i], base_name, sizeof(base_name));

    jdebug_log_printf(log, "  0x%p %s\n", module_info.lpBaseOfDll, file_name);

    SymLoadModuleEx(process,
                    NULL,
                    file_name,
                    base_name,
                    (DWORD64) module_info.lpBaseOfDll,
                    module_info.SizeOfImage,
                    NULL,
                    0);
  }

  free(modules);
}

static void print_stack(logger_t* log, CONTEXT* ctx, size_t skip)
{
  CONTEXT Context;
  memcpy(&Context, ctx, sizeof(CONTEXT));

  STACKFRAME64 StackFrame;
  DWORD MachineType;
#ifdef _M_IX86
  MachineType                 = IMAGE_FILE_MACHINE_I386;
  StackFrame.AddrPC.Offset    = Context.Eip;
  StackFrame.AddrPC.Mode      = AddrModeFlat;
  StackFrame.AddrFrame.Offset = Context.Ebp;
  StackFrame.AddrFrame.Mode   = AddrModeFlat;
  StackFrame.AddrStack.Offset = Context.Esp;
  StackFrame.AddrStack.Mode   = AddrModeFlat;
#elif _M_X64
  MachineType                 = IMAGE_FILE_MACHINE_AMD64;
  StackFrame.AddrPC.Offset    = Context.Rip;
  StackFrame.AddrPC.Mode      = AddrModeFlat;
  StackFrame.AddrFrame.Offset = Context.Rsp;
  StackFrame.AddrFrame.Mode   = AddrModeFlat;
  StackFrame.AddrStack.Offset = Context.Rsp;
  StackFrame.AddrStack.Mode   = AddrModeFlat;
#elif _M_IA64
  MachineType                  = IMAGE_FILE_MACHINE_IA64;
  StackFrame.AddrPC.Offset     = Context.StIIP;
  StackFrame.AddrPC.Mode       = AddrModeFlat;
  StackFrame.AddrFrame.Offset  = Context.IntSp;
  StackFrame.AddrFrame.Mode    = AddrModeFlat;
  StackFrame.AddrBStore.Offset = Context.RsBSP;
  StackFrame.AddrBStore.Mode   = AddrModeFlat;
  StackFrame.AddrStack.Offset  = Context.IntSp;
  StackFrame.AddrStack.Mode    = AddrModeFlat;
#else
#error "unsupported architecture"
#endif

  jdebug_log_printf(log, "stack:\n");

  size_t frame_num = 0;

  while (1) {
    if (frame_num >= skip) {
      IMAGEHLP_MODULE module_info;
      ZeroMemory(&module_info, sizeof(module_info));
      module_info.SizeOfStruct = sizeof(module_info);

      SymGetModuleInfo(GetCurrentProcess(), StackFrame.AddrPC.Offset, &module_info);

      symbol_info_t symbol_info = {0};
      jdebug_find_symbol(&module_info, StackFrame.AddrPC.Offset, &symbol_info);

      jdebug_log_printf(log, "%3llu %s[0x%llX(0x%llX)+0x%llX] %s\n", frame_num - skip,
                        module_info.ImageName,
                        StackFrame.AddrPC.Offset,
                        symbol_info.start_address,
                        symbol_info.displacement,
                        symbol_info.name ? symbol_info.name : "???");

      if (symbol_info.filename)
        jdebug_log_printf(log, "    %s:%llu\n", symbol_info.filename, symbol_info.line);

      free(symbol_info.name);
      free(symbol_info.filename);
    }

    DWORD64 offset = StackFrame.AddrPC.Offset;
  walk:
    SetLastError(0);
    if (!StackWalk64(
          MachineType,
          GetCurrentProcess(),
          GetCurrentThread(),
          &StackFrame,
          MachineType == IMAGE_FILE_MACHINE_I386
            ? NULL
            : &Context,
          NULL,
          SymFunctionTableAccess64,
          SymGetModuleBase64,
          NULL)) {
      break;
    }

    if (StackFrame.AddrPC.Offset == 0)
      break;

    // under mingw StackWalk64 returns the same PC on first call
    if (!frame_num && offset == StackFrame.AddrPC.Offset) {
      offset = 0; // jic prevent infinite loop
      goto walk;
    }

    frame_num++;
  }
}

static void jdebug_write_core_dump(const logger_t* log, const wchar_t* dump_file_path, PEXCEPTION_POINTERS exception_pointers)
{
  HANDLE dump_file = CreateFileW(dump_file_path,
                                 GENERIC_WRITE,
                                 0,
                                 NULL,
                                 CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL,
                                 NULL);

  if (INVALID_HANDLE_VALUE == dump_file) {
    jdebug_log_printf(log, "failed to open file for mini dump %ld\n", GetLastError());
    return;
  }

  MINIDUMP_EXCEPTION_INFORMATION exception_information;
  exception_information.ThreadId          = GetCurrentThreadId();
  exception_information.ClientPointers    = TRUE;
  exception_information.ExceptionPointers = exception_pointers;

  if (!MiniDumpWriteDump(GetCurrentProcess(),
                         GetCurrentProcessId(),
                         dump_file,
                         MiniDumpNormal,
                         &exception_information,
                         NULL,
                         NULL)) {
    jdebug_log_printf(log, "failed to write minidump 0x%lX\n", GetLastError());
  }

  FlushFileBuffers(dump_file);
  CloseHandle(dump_file);
}

static void jdebug_suspend_other_threads()
{
  HANDLE snapshot_handle = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  DWORD current_thread   = GetCurrentThreadId();
  DWORD current_process  = GetCurrentProcessId();

  if (snapshot_handle != INVALID_HANDLE_VALUE) {
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    if (Thread32First(snapshot_handle, &te)) {
      do {
        if (te.dwSize >= FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(te.th32OwnerProcessID)) {
          if (te.th32ThreadID != current_thread && te.th32OwnerProcessID == current_process) {
            HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (thread) {
              SuspendThread(thread);
              CloseHandle(thread);
            }
          }
        }
        te.dwSize = sizeof(te);
      } while (Thread32Next(snapshot_handle, &te));
    }
    CloseHandle(snapshot_handle);
  }
}

// most recent c++ exception raised on each thread (see `jdebug_exception_filter_first`)
static JDEBUG_THREAD_LOCAL EXCEPTION_RECORD g_exception_record;
static JDEBUG_THREAD_LOCAL CONTEXT g_context;
// set by the abort handler on the aborting thread
static JDEBUG_THREAD_LOCAL int g_in_abort;
static int g_primary_abort = 0;

static LONG WINAPI jdebug_exception_filter(LPEXCEPTION_POINTERS exception_info_p)
{
  // only one thread may handle a crash, any other thread crashing concurrently waits to be terminated
  if (InterlockedCompareExchange(&g_handling_crash, 1, 0) != 0) {
    for (;;)
      Sleep(INFINITE);
  }

  EXCEPTION_POINTERS exception_pointers = *exception_info_p;
  // abort() caused by an unhandled c++ exception: report the exception rather than the abort
  if (g_in_abort && g_exception_record.ExceptionCode) {
    exception_pointers.ExceptionRecord = &g_exception_record;
    exception_pointers.ContextRecord   = &g_context;
  }

  jdebug_suspend_other_threads();

  CONTEXT lctx;
  memcpy(&lctx, exception_pointers.ContextRecord, sizeof(CONTEXT));

  if (g_use_temp_log_filename)
    jdebug_set_temp_log_filename();

  logger_t log;
  jdebug_log_init(g_log_filename[0] ? g_log_filename : NULL, &log);

  DWORD exit_code;
  if (g_primary_abort) {
    jdebug_log_printf(&log, "fatal exception occurred: abort called\n");
    exit_code = 3;
  } else {
    exit_code = jdebug_print_exception(&log, &exception_pointers);
  }

  if (g_log_filename[0]) {
    char log_filename_utf8[MAX_PATH * 4];
    if (WideCharToMultiByte(CP_UTF8, 0, g_log_filename, -1, log_filename_utf8, sizeof(log_filename_utf8), NULL, NULL))
      jdebug_log_printf(&log, "log file: %s\n", log_filename_utf8);
  }

  // registered are meaningless by the time we get to sigabrt handler, no point in printing them
  if (!g_primary_abort)
    jdebug_print_context(&log, exception_pointers.ContextRecord);

  if (g_core_filename[0])
    jdebug_write_core_dump(&log, g_core_filename, &exception_pointers);

  jdebug_init_symbol_handler();
  jdebug_print_modules(&log);
  // skip the frames of the abort machinery (abort -> signal handler -> __debugbreak)
  print_stack(&log, &lctx, g_primary_abort ? 4 : 0);

  SymCleanup(GetCurrentProcess());
  jdebug_log_deinit(&log);

  TerminateProcess(GetCurrentProcess(), exit_code);
  return EXCEPTION_EXECUTE_HANDLER;
}

// some exceptions (eg. c++ exception in threads) don't hit the UnhandledExceptionFilter
//  but do hit a VectoredExceptionHandler then calls abort(). It's the abort handler that redirects
//  to the UnhandledExceptionFilter but grab the Exception/Context details first.
// Only c++ exceptions are recorded (other exceptions, eg. handled SEH exceptions, are irrelevant to abort),
//  and always the most recent one per thread, since earlier exceptions may have been caught.
static LONG WINAPI jdebug_exception_filter_first(LPEXCEPTION_POINTERS exception_info)
{
  const DWORD code = exception_info->ExceptionRecord->ExceptionCode;
  if (code == JDEBUG_CXX_EXCEPTION_CODE_MSVC || code == JDEBUG_CXX_EXCEPTION_CODE_GCC) {
    memcpy(&g_exception_record, exception_info->ExceptionRecord, sizeof(EXCEPTION_RECORD));
    memcpy(&g_context, exception_info->ContextRecord, sizeof(CONTEXT));
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void jdebug_abort_handler(int signo)
{
  (void) signo;

  g_in_abort = 1;
  if (g_exception_record.ExceptionCode == 0)
    g_primary_abort = 1;

  // redirects to exception filter
  __debugbreak();
}
