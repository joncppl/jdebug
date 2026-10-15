#include "jdebug_priv.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>

#include <sys/ptrace.h>
#include <sys/wait.h>

int jdebug_enumerate_tasks(pid_t pid, jdebug_tasks_t* tasks)
{
  char* taskdir         = NULL;
  DIR* dir              = NULL;
  struct dirent* dirent = NULL;
  int ret               = 0;

  *tasks = (jdebug_tasks_t) jcontainer_array_init;

  if (asprintf(&taskdir, "/proc/%d/task", pid) < 0 || !taskdir) {
    ret = -1;
    goto done;
  }

  dir = opendir(taskdir);

  if (!dir) {
    ret = -1;
    goto done;
  }

  while ((dirent = readdir(dir))) {
    if (dirent->d_name[0] == '.') {
      continue;
    }

    pid_t tid = (pid_t) strtol(dirent->d_name, NULL, 10);
    if (jcontainer_array_push_back(tasks, &tid)) {
      ret = -1;
      jcontainer_array_fini(tasks);
      break;
    }
  }

done:
  if (dir)
    closedir(dir);
  free(taskdir);

  return ret;
}

int jdebug_pause_tasks(jdebug_tasks_t* tasks)
{
  int err = 0;

  for (size_t i = 0; i < tasks->size; ++i) {
    if (-1 == ptrace(PTRACE_ATTACH, tasks->base[i], NULL, NULL)) {
      perror("PTRACE_ATTACH");
      err = -1;
    }
  }

  // PTRACE_ATTACH only sends SIGSTOP, wait until each task is actually stopped,
  //  otherwise subsequent ptrace requests (eg. PTRACE_GETREGS) may fail with ESRCH.
  for (size_t i = 0; i < tasks->size; ++i) {
    int status = 0;
    pid_t r;
    while (-1 == (r = waitpid(tasks->base[i], &status, __WALL)) && errno == EINTR) {
      /* no-op */
    }
    if (-1 == r) {
      perror("waitpid");
      err = -1;
    }
  }
  return err;
}

int jdebug_resume_tasks(jdebug_tasks_t* tasks)
{
  struct timespec stime = {0, 100 * 1000 * 1000};
  int r;
  while (((r = nanosleep(&stime, &stime))) && errno == EINTR) {
    /* no-op */
  }

  for (size_t i = 0; i < tasks->size; ++i) {
    if (-1 == ptrace(PTRACE_DETACH, tasks->base[i], NULL, NULL)) {
      perror("PTRACE_DETACH");
      r = -1;
    }
  }

  return r;
}
