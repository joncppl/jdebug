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

static wchar_t g_log_filename[MAX_PATH]  = {0};
static wchar_t g_core_filename[MAX_PATH] = {0};
static _Atomic int g_init_count          = 0;

LPTOP_LEVEL_EXCEPTION_FILTER g_prev_exception_filter = NULL;
static void(__cdecl* g_prev_sigabrt)(int)            = SIG_IGN;
unsigned int g_prev_abort_behavior                   = 0;
#define JDEBUG_ABORT_BEHAVIOUR_MASK (_WRITE_ABORT_MSG | _CALL_REPORTFAULT)

static void jdebug_abort_handler(int signo);
static LONG jdebug_exception_filter(LPEXCEPTION_POINTERS exception_info);

static void jdebug_set_temp_log_filename()
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
      if (!_wfullpath(g_log_filename, init_info->log_filename, MAX_PATH))
        ZeroMemory(g_log_filename, MAX_PATH);
    } else {
      jdebug_set_temp_log_filename();
    }
    if (NULL != init_info->coredump_filename) {
      if (!_wfullpath(g_core_filename, init_info->coredump_filename, MAX_PATH))
        ZeroMemory(g_core_filename, MAX_PATH);
    }

    g_prev_exception_filter = SetUnhandledExceptionFilter(jdebug_exception_filter);
    g_prev_sigabrt          = signal(SIGABRT, jdebug_abort_handler);
    g_prev_abort_behavior   = _set_abort_behavior(0, JDEBUG_ABORT_BEHAVIOUR_MASK);
  }
}

void jdebug_crash_handler_deinit()
{
  const int val = atomic_fetch_sub(&g_init_count, 1);
  if (val == 1) {
    _set_abort_behavior(g_prev_abort_behavior, JDEBUG_ABORT_BEHAVIOUR_MASK);
    g_prev_abort_behavior = 0;

    signal(SIGABRT, g_prev_sigabrt);
    g_prev_sigabrt = SIG_IGN;

    SetUnhandledExceptionFilter(g_prev_exception_filter);
    g_prev_abort_behavior = 0;

    ZeroMemory(g_log_filename, MAX_PATH * sizeof(wchar_t));
    ZeroMemory(g_core_filename, MAX_PATH * sizeof(wchar_t));
  } else if (val <= 0) {
    fprintf(stderr, "jdebug_crash_handler_deinit called too many times\n");
    abort();
  }
}

#ifdef JDEBUG_PRINT_EXCEPTION
void jdebug_print_cpp_exception(const logger_t* log, DWORD exception_code, ULONG_PTR* exception_information);
#endif

static void print_exception(logger_t* log, LPEXCEPTION_POINTERS exception_info)
{
  DWORD code = exception_info->ExceptionRecord->ExceptionCode;

  jdebug_log_printf(log, "fatal exception occurred: ");

  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
      jdebug_log_printf(log, "Access Violation");
      break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
      jdebug_log_printf(log, "Array Bounds Exceeded");
      break;
    case EXCEPTION_BREAKPOINT:
      jdebug_log_printf(log, "Breakpoint");
      break;
    case EXCEPTION_DATATYPE_MISALIGNMENT:
      jdebug_log_printf(log, "Datatype Misalignment");
      break;
    case EXCEPTION_FLT_DENORMAL_OPERAND:
      jdebug_log_printf(log, "FLT Denormal Operand");
      break;
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
      jdebug_log_printf(log, "FLT Divide By Zero");
      break;
    case EXCEPTION_FLT_INEXACT_RESULT:
      jdebug_log_printf(log, "FLT Inexact Result");
      break;
    case EXCEPTION_FLT_INVALID_OPERATION:
      jdebug_log_printf(log, "FLT Invalid Operation");
      break;
    case EXCEPTION_FLT_OVERFLOW:
      jdebug_log_printf(log, "FLT Overflow");
      break;
    case EXCEPTION_FLT_STACK_CHECK:
      jdebug_log_printf(log, "FLT Stack Check");
      break;
    case EXCEPTION_FLT_UNDERFLOW:
      jdebug_log_printf(log, "FLT Underflow");
      break;
    case EXCEPTION_GUARD_PAGE:
      jdebug_log_printf(log, "Guard Page");
      break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
      jdebug_log_printf(log, "Illegal Instruction");
      break;
    case EXCEPTION_IN_PAGE_ERROR:
      jdebug_log_printf(log, "In Page Error");
      break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
      jdebug_log_printf(log, "INT Divide By Zero");
      break;
    case EXCEPTION_INT_OVERFLOW:
      jdebug_log_printf(log, "INT Overflow");
      break;
    case EXCEPTION_INVALID_DISPOSITION:
      jdebug_log_printf(log, "Invalid Disposition");
      break;
    case EXCEPTION_INVALID_HANDLE:
      jdebug_log_printf(log, "Invalid Handle");
      break;
    case EXCEPTION_NONCONTINUABLE_EXCEPTION:
      jdebug_log_printf(log, "Noncontinuable Exception");
      break;
    case EXCEPTION_PRIV_INSTRUCTION:
      jdebug_log_printf(log, "Priv Instruction");
      break;
    case EXCEPTION_SINGLE_STEP:
      jdebug_log_printf(log, "Single Step");
      break;
    case EXCEPTION_STACK_OVERFLOW:
      jdebug_log_printf(log, "Stack Overflow");
      break;
    case STATUS_UNWIND_CONSOLIDATE:
      jdebug_log_printf(log, "Unwind Consolidate");
      break;
    case 0xE06D7363:
    case 0x20474343:
#ifdef JDEBUG_PRINT_EXCEPTION
      jdebug_log_printf(log, "Unhandled C++ Exception");
      jdebug_print_cpp_exception(log, exception_info->ExceptionRecord->ExceptionCode, exception_info->ExceptionRecord->ExceptionInformation);
#else
      jdebug_log_printf(log, "Unhandled C++ Exception\n");
#endif
      break;
    default:
      jdebug_log_printf(log, "Unknown Exception (%ld)", exception_info->ExceptionRecord->ExceptionCode);
      break;
  }

  jdebug_log_printf(log, "\n");
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
                    ctx->Rdi,
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

static void jdebug_init_symbol_handler()
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
  EnumProcessModules(process, modules, bytes_needed, &bytes_needed);

  jdebug_log_printf(log, "loaded modules:\n");

  DWORD module_count = bytes_needed / sizeof(HMODULE);
  for (DWORD i = 0; i < module_count; i++) {
    MODULEINFO module_info;
    char file_name[MAX_PATH + 1];
    char base_name[MAX_PATH + 1];
    GetModuleInformation(process, modules[i], &module_info, sizeof(module_info));
    GetModuleFileNameEx(process, modules[i], file_name, sizeof(file_name));
    GetModuleBaseName(process, modules[i], base_name, sizeof(base_name));

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

static void print_stack(logger_t* log, CONTEXT* ctx)
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
    SetLastError(0);

    IMAGEHLP_MODULE module_info;
    ZeroMemory(&module_info, sizeof(module_info));
    module_info.SizeOfStruct = sizeof(module_info);

    SymGetModuleInfo(GetCurrentProcess(), StackFrame.AddrPC.Offset, &module_info);

    symbol_info_t symbol_info = {0};
    jdebug_find_symbol(&module_info, StackFrame.AddrPC.Offset, &symbol_info);

    jdebug_log_printf(log, "%3llu %s[0x%llX(0x%llX)+0x%llX] %s\n", frame_num,
                      module_info.ImageName,
                      StackFrame.AddrPC.Offset,
                      symbol_info.start_address,
                      symbol_info.displacement,
                      symbol_info.name ? symbol_info.name : "???");

    if (symbol_info.filename)
      jdebug_log_printf(log, "    %s:%llu\n", symbol_info.filename, symbol_info.line);

    free(symbol_info.name);
    free(symbol_info.filename);

    DWORD64 offset = StackFrame.AddrPC.Offset;
  walk:
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
            SuspendThread(thread);
          }
        }
        te.dwSize = sizeof(te);
      } while (Thread32Next(snapshot_handle, &te));
    }
    CloseHandle(snapshot_handle);
  }
}

static LONG jdebug_exception_filter(LPEXCEPTION_POINTERS exception_info)
{
  jdebug_suspend_other_threads();

  CONTEXT lctx;
  memcpy(&lctx, exception_info->ContextRecord, sizeof(CONTEXT));

  logger_t log;
  jdebug_log_init(g_log_filename, &log);

  print_exception(&log, exception_info);
  jdebug_print_context(&log, exception_info->ContextRecord);

  if (g_core_filename[0])
    jdebug_write_core_dump(&log, g_core_filename, exception_info);

  jdebug_init_symbol_handler();
  jdebug_print_modules(&log);
  print_stack(&log, &lctx);

  SymCleanup(GetCurrentProcess());
  jdebug_log_deinit(&log);

  return EXCEPTION_EXECUTE_HANDLER;
}

static void jdebug_abort_handler(int signo)
{
  (void) signo;
  __debugbreak(); // redirect to exception filter
                  // however ContextRecord will be wrong and this function
                  // will show up stack trace
}
