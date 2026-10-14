#pragma once

#include "container.h"
#include "jdebug_log.h"

#include <libunwind.h>
#include <signal.h>
#include <stdio.h>

int jdebug_run_child(pid_t crash_pid,
                     pid_t crash_tid,
                     const char* coredump_file,
                     const char* log_file);

void jdebug_install_signal_handlers();

void jdebug_uninstall_signal_handlers();

void jdebug_print_signal_info(const logger_t* log,
                              const siginfo_t* info,
                              const void* ucontext);

JCONTAINER_DECLARE_ARRAY(pid_t, jdebug_tasks_t)

int jdebug_enumerate_tasks(pid_t pid, jdebug_tasks_t* tasks);
int jdebug_pause_tasks(jdebug_tasks_t* tasks);
int jdebug_resume_tasks(jdebug_tasks_t* tasks);

typedef enum
{
  MAPPING_ACCESS_READ  = 0x01,
  MAPPING_ACCESS_WRITE = 0x02,
  MAPPING_ACCESS_EXEC  = 0x04,
  MAPPING_ACCESS_SHARE = 0x08,
  MAPPING_ACCESS_PRIV  = 0x10,
} mapping_access_t;

typedef struct mapping
{
  size_t s_addr;
  size_t e_addr;
  size_t offset;
  mapping_access_t access;
  char* pathname;

  size_t load_bias;

  JCONTAINER_LL_FIELD_TYPED(struct mapping)
} mapping_t;

mapping_t* jdebug_mappings_load(pid_t pid);
void jdebug_mappings_free(mapping_t*);
size_t jdebug_mapping_size(const mapping_t* mapping);
size_t jdebug_mapping_memory_to_object(const mapping_t* mapping, size_t memory_addr);
const mapping_t* jdebug_mapping_lookup(const mapping_t* mappings, size_t addr);
void jdebug_mapping_print(const mapping_t* mapping, const logger_t* log);
int jdebug_mapping_get_source_file(const mapping_t* mapping, size_t addr, char** source_file, uint64_t* source_line);

typedef struct
{
  unw_word_t ip;
  unw_proc_info_t proc_info;
  unw_word_t proc_offset;
  size_t name_len;
} stack_frame_t;

typedef struct stack_frame_list
{
  stack_frame_t frame;
  char* name;

  JCONTAINER_LL_FIELD_TYPED(struct stack_frame_list)
} stack_frame_list_t;

int jdebug_collect_and_write_stacktrace(int stdin_fd, void* ucontext);
stack_frame_list_t* jdebug_read_stack_trace(int fd);
void jdebug_free_stacktrace(stack_frame_list_t* trace);
void jdebug_print_stack_trace(const logger_t* log, stack_frame_list_t* trace, mapping_t* mappings);

long jdebug_gettime_monotonic_ms();
ssize_t jdebug_read_all(int fd, void* buf, size_t size, long timeout_ms);
ssize_t jdebug_write_all(int fd, const void* buf, size_t size);
char* jdebug_demangle(const char* mangled);
char* jdebug_demangle_safe(const char* mangled);
void jdebug_demangle_safe_free(const char* demangled);

#define JDEBUG_DEMANGLE_ARG "_jdc_demangle"

int jdebug_coredump_write(pid_t process,
                          pid_t crash_task,
                          const char* filename,
                          const pid_t* tasks,
                          size_t n_tasks,
                          const mapping_t* mappings,
                          const siginfo_t* siginfo,
                          const ucontext_t* ucontext);

#define JDEBUG_DEFAULT_TIMEOUT_MS (1000)

#define jdebug_round_down_power_of_2(value_arg_, multiple_arg_) \
  __extension__({                                               \
    __typeof__((value_arg_)) value__       = (value_arg_);      \
    __typeof__((multiple_arg_)) multiple__ = (multiple_arg_);   \
    value__ & ~(multiple__ - 1);                                \
  })

#define jdebug_round_up_power_of_2(value_arg_, multiple_arg_) \
  __extension__({                                             \
    __typeof__((value_arg_)) value__       = (value_arg_);    \
    __typeof__((multiple_arg_)) multiple__ = (multiple_arg_); \
    (value__ + multiple__ - 1) & ~(multiple__ - 1);           \
  })
