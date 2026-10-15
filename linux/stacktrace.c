#include "jdebug_priv.h"

#include <stdlib.h>
#include <string.h>

#include <libunwind.h>

int jdebug_collect_and_write_stacktrace(int stdin_fd, void* ucontext)
{
  size_t max_depth          = 1024;
  size_t depth              = 0;
  stack_frame_t stack_frame = {0};
  unw_cursor_t cursor       = {0};

#ifdef unw_init_local2
  if (unw_init_local2(&cursor, ucontext, UNW_INIT_SIGNAL_FRAME))
    return -1;
#else
  if (unw_init_local(&cursor, ucontext))
    return -1;
#endif

  ssize_t ret = 0;
  do {
    unw_get_reg(&cursor, UNW_REG_IP, &stack_frame.ip);

    if (unw_get_proc_info(&cursor, &stack_frame.proc_info)) {
      memset(&stack_frame.proc_info, 0, sizeof(stack_frame.proc_info));
    }

    char sym_name[1024];
    if (unw_get_proc_name(&cursor, sym_name, sizeof(sym_name), &stack_frame.proc_offset)) {
      stack_frame.proc_offset = 0;
      sym_name[0]             = 0;
    }
    stack_frame.name_len = strlen(sym_name);

    if ((ret = jdebug_write_all(stdin_fd, &stack_frame, sizeof(stack_frame))) < 0) {
      return (int) ret;
    }

    if ((ret = jdebug_write_all(stdin_fd, sym_name, stack_frame.name_len)) < 0) {
      return (int) ret;
    }
  } while ((ret = unw_step(&cursor)) > 0 && ++depth < max_depth);

  memset(&stack_frame, 0, sizeof(stack_frame));
  if ((ret = jdebug_write_all(stdin_fd, &stack_frame, sizeof(stack_frame))) < 0) {
    return (int) ret;
  }

  return 0;
}

stack_frame_list_t* jdebug_read_stack_trace(int fd)
{
  stack_frame_list_t* trace = NULL;

  const size_t max_stack_trace = 1024;
  for (size_t i = 0; i < max_stack_trace; ++i) {
    stack_frame_list_t frame;

    if (jdebug_read_all(fd, &frame.frame, sizeof(stack_frame_t), JDEBUG_DEFAULT_TIMEOUT_MS) < 0)
      goto error;

    if (!frame.frame.ip)
      break;

    frame.name = calloc(frame.frame.name_len + 1, 1);
    if (!frame.name)
      goto error;

    if (jdebug_read_all(fd, frame.name, frame.frame.name_len, JDEBUG_DEFAULT_TIMEOUT_MS) < 0) {
      free(frame.name);
      goto error;
    }

    frame.name[frame.frame.name_len] = '\0';

    if (jcontainer_ll_push_back(&trace, &frame)) {
      free(frame.name);
      goto error;
    }
  }

  return trace;

error:
  jdebug_free_stacktrace(trace);
  return NULL;
}

void jdebug_free_stacktrace(stack_frame_list_t* trace)
{
  jcontainer_ll_foreach(&trace, frame)
  {
    free(frame->name);
  }
  jcontainer_ll_clear(&trace);
}

void jdebug_print_stack_trace(const logger_t* log, stack_frame_list_t* trace, mapping_t* mappings)
{
  jdebug_log_format(log, "stack trace:\n");

  size_t depth        = 0;
  size_t stack_offset = 0;
  jcontainer_ll_foreach(&trace, frame)
  {
    const mapping_t* map = jdebug_mapping_lookup(mappings, frame->frame.ip);

    char* demangled_name     = jdebug_demangle(frame->name);
    const char* display_name = demangled_name ? demangled_name : frame->name;

    if (map) {
      jdebug_log_format(log, "%3zu %s[0x%zX(0x%zX)+0x%zX] %s\n",
                        depth - stack_offset, map->pathname, frame->frame.ip,
                        frame->frame.proc_info.start_ip - map->s_addr + map->offset,
                        frame->frame.proc_offset, display_name);
    } else {
      jdebug_log_format(log, "%3zu %s[0x%zX+0x%zX] %s\n",
                        depth - stack_offset, "unknown", frame->frame.ip, frame->frame.proc_offset, display_name);
    }

    if (map) {
      const size_t memory_ip = depth == 0 ? frame->frame.ip : frame->frame.ip - 1;
      const size_t object_ip = jdebug_mapping_memory_to_object(map, memory_ip);

      char* src;
      uint64_t line;
      if (!jdebug_mapping_get_source_file(map, object_ip, &src, &line)) {
        jdebug_log_format(log, "    %s:%lu\n", src, line);
        free(src);
      }
    }

    free(demangled_name);
    ++depth;
  }
}
