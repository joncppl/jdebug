#include "jdebug_priv.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct
{
  logger_t log;
  pid_t crash_pid;
  pid_t crash_tid;
  const char* coredump_file;
  siginfo_t siginfo;
  ucontext_t ucontext;
  jdebug_tasks_t tasks;
  mapping_t* mappings;
  stack_frame_list_t* stack_trace;
} crash_state_t;

static int jdebug_run(crash_state_t* state);

int jdebug_run_child(pid_t crash_pid,
                     pid_t crash_tid,
                     const char* coredump_file,
                     const char* log_file)
{
  int r               = 1;
  crash_state_t state = {0};
  state.crash_pid     = crash_pid;
  state.crash_tid     = crash_tid;
  state.coredump_file = coredump_file;

  jdebug_log_init(&state.log, log_file);

  if (jdebug_read_all(STDIN_FILENO, &state.siginfo, sizeof(siginfo_t), JDEBUG_DEFAULT_TIMEOUT_MS) < 0) {
    const int lerrno = errno;
    jdebug_log_format(&state.log, "error occurred handling crash\n");
    jdebug_log_format(&state.log, "failed to poll stdin: %s\n", strerror(lerrno));
    goto done;
  }

  if (jdebug_read_all(STDIN_FILENO, &state.ucontext, sizeof(ucontext_t), JDEBUG_DEFAULT_TIMEOUT_MS) < 0) {
    const int lerrno = errno;
    jdebug_log_format(&state.log, "error occurred handling crash\n");
    jdebug_log_format(&state.log, "failed to poll stdin: %s\n", strerror(lerrno));
    goto done;
  }

  if (!((state.stack_trace = jdebug_read_stack_trace(STDIN_FILENO)))) {
    const int lerrno = errno;
    jdebug_log_format(&state.log, "error occurred handling crash\n");
    jdebug_log_format(&state.log, "failed to poll stdin: %s\n", strerror(lerrno));
    goto done;
  }

  r = jdebug_run(&state);

done:
  if (state.stack_trace)
    jdebug_free_stacktrace(state.stack_trace);
  jdebug_log_deinit(&state.log);
  return r;
}

int jdebug_run(crash_state_t* state)
{
  int ret = 0;

  if (-1 == jdebug_enumerate_tasks(state->crash_pid, &state->tasks)) {
    const int lerrno = errno;
    jdebug_log_format(&state->log, "error occurred handling crash\n");
    jdebug_log_format(&state->log, "failed to poll stdin: %s\n", strerror(lerrno));
    return 1;
  }

  if (jdebug_pause_tasks(&state->tasks)) {
    ret = 1;
    goto error;
  }

  state->mappings = jdebug_mappings_load(state->crash_pid);
  if (!state->mappings) {
    ret = 1;
    goto error;
  }

  jdebug_log_format(&state->log, "mappings:\n");
  jcontainer_ll_foreach(&state->mappings, mapping)
  {
    jdebug_mapping_print(mapping, &state->log);
  }

  jdebug_print_stack_trace(&state->log, state->stack_trace, state->mappings);

  if (state->coredump_file && strlen(state->coredump_file)) {
    jdebug_log_format(&state->log, "writing core to %s\n", state->coredump_file);
    if (jdebug_coredump_write(state->crash_pid,
                              state->crash_tid,
                              state->coredump_file,
                              state->tasks.base,
                              state->tasks.size,
                              state->mappings,
                              &state->siginfo,
                              &state->ucontext)) {
      const int lerrno = errno;
      jdebug_log_format(&state->log, "error occurred handling crash\n");
      jdebug_log_format(&state->log, "failed to write coredump: %s\n", strerror(lerrno));
      ret = 1;
      goto error;
    }
    jdebug_log_format(&state->log, "finished writing core\n");
  }

  ret = 0;

error:
  jdebug_mappings_free(state->mappings);
  jdebug_resume_tasks(&state->tasks);
  jcontainer_array_fini(&state->tasks);
  return ret;
}
