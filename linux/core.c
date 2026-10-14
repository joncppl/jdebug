#include "jdebug_priv.h"

#include <stdlib.h>
#include <string.h>

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <sys/procfs.h>
#include <sys/ptrace.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#define JDEBUG_SECTION_NAME_SHSTRTAB_INDEX 0
#define JDEBUG_SECTION_NAME_NOTE_INDEX 1
#define JDEBUG_SECTION_NAME_LOAD_INDEX 2
#define JDEBUG_SECTION_NAMES {".shstrtab", "note0", "load"}

#define JDEBUG_N_SECTION_NAMES 3

typedef enum
{
  JDEBUG_COREDUMP_COMPRESSOR_NONE  = 0,
  JDEBUG_COREDUMP_COMPRESSOR_ZSTD  = 1,
  JDEBUG_COREDUMP_COMPRESSOR_XZ    = 2,
  JDEBUG_COREDUMP_COMPRESSOR_BZIP2 = 3,
  JDEBUG_COREDUMP_COMPRESSOR_GZIP  = 4,
  JDEBUG_COREDUMP_COMPRESSOR_LAST  = JDEBUG_COREDUMP_COMPRESSOR_GZIP,
} compressor_t;

typedef struct
{
  pid_t process;
  pid_t crash_task;
  const char* filename;
  const pid_t* tasks;
  size_t n_tasks;
  const mapping_t* mappings;
  const siginfo_t* siginfo;
  const ucontext_t* ucontext;

  size_t n_mappings;

  compressor_t compressor;
  int output_fd;
  pid_t compressor_pid;
  size_t n_wrote;

  size_t page_size;
  char* auxv_path;

  // header data
  size_t n_phdr;
  size_t n_shdr;
  size_t phdr_start;
  size_t shdr_start;
  size_t data_start;
  size_t notes_start;
  size_t notes_size;
  size_t string_tab_start;
  size_t string_table_size;
  size_t string_indices[JDEBUG_N_SECTION_NAMES];
  size_t n_auxv;
} coredump_state_t;

static int jdebug_coredump_write_internal(coredump_state_t* state);
static int jdebug_coredump_prepare_offsets(coredump_state_t* state);
static int jdebug_coredump_write_headers(coredump_state_t* state);
static int jdebug_coredump_write_data_sections(coredump_state_t* state);
static int jdebug_coredump_write_notes(coredump_state_t* state);
static int jdebug_coredump_write_prpsinfo(coredump_state_t* state);
static int jdebug_coredump_write_prstatus(coredump_state_t* state);
static int jdebug_coredump_write_auxv(coredump_state_t* state);
static int jdebug_coredump_write_string_table(coredump_state_t* state);
static int jdebug_coredump_seek(coredump_state_t* state, size_t pos);
static int jdebug_coredump_do_write(coredump_state_t* state, const void* buf, size_t len);
static compressor_t jdebug_coredump_choose_compressor();
static int jdebug_coredump_open_compressor(coredump_state_t* state);
static int jdebug_coredump_close_compressor(coredump_state_t* state);


int jdebug_coredump_write(pid_t process,
                          pid_t crash_task,
                          const char* filename,
                          const pid_t* tasks,
                          const size_t n_tasks,
                          const mapping_t* mappings,
                          const siginfo_t* siginfo,
                          const ucontext_t* ucontext)
{
  long page_size          = 0;
  int ret                 = 0;
  coredump_state_t* state = NULL;

  if (-1 == (page_size = sysconf(_SC_PAGESIZE)))
    return -1;

  state = malloc(sizeof(coredump_state_t));
  if (!state)
    return -1;

  memset(state, 0, sizeof(coredump_state_t));

  state->process    = process;
  state->crash_task = crash_task;
  state->filename   = filename;
  state->tasks      = tasks;
  state->n_tasks    = n_tasks;
  state->mappings   = mappings;
  state->siginfo    = siginfo;
  state->ucontext   = ucontext;

  state->page_size = page_size;

  state->compressor = jdebug_coredump_choose_compressor();

  jcontainer_ll_size(&mappings, &state->n_mappings);

  ret = jdebug_coredump_write_internal(state);
  free(state);
  return ret;
}

static int jdebug_coredump_write_internal(coredump_state_t* state)
{
  int ret = -1;

  if (jdebug_coredump_open_compressor(state))
    goto done;

  state->auxv_path = NULL;
  if (asprintf(&state->auxv_path, "/proc/%d/auxv", state->process) < 0 || !state->auxv_path)
    goto done;

  if (jdebug_coredump_prepare_offsets(state))
    goto done;

  if (jdebug_coredump_write_headers(state))
    goto done;

  if (jdebug_coredump_write_data_sections(state))
    goto done;

  if (jdebug_coredump_write_notes(state))
    goto done;

  if (jdebug_coredump_write_string_table(state))
    goto done;

  ret = 0;

done:
  jdebug_coredump_close_compressor(state);
  free(state->auxv_path);
  return ret;
}

static int jdebug_coredump_prepare_offsets(coredump_state_t* state)
{
  int ret = -1;

  state->n_phdr = state->n_mappings + 1; // + notes
  state->n_shdr = state->n_mappings + 2; // + notes + strtab

  state->phdr_start = sizeof(Elf64_Ehdr);
  state->shdr_start = state->phdr_start + state->n_phdr * sizeof(Elf64_Phdr);
  state->data_start = state->shdr_start + state->n_shdr * sizeof(Elf64_Shdr);

  const int auxv_fd = open(state->auxv_path, O_RDONLY);
  if (-1 == auxv_fd) {
    perror("open auxv");
    return -1;
  }

  state->n_auxv = 0;
  ssize_t r;
  Elf64_auxv_t auxv;
  while ((r = jdebug_read_all(auxv_fd, &auxv, sizeof(auxv), -1)) == sizeof(auxv))
    ++state->n_auxv;
  if (-1 == r)
    goto done;

  state->notes_size = (sizeof(Elf64_Nhdr) + 8) * (2 * state->n_tasks + 2)
                      + sizeof(struct elf_prpsinfo)
                      + (sizeof(struct elf_prstatus) + sizeof(prfpregset_t)) * state->n_tasks
                      + state->n_auxv * sizeof(Elf64_auxv_t);

  const char* strings[JDEBUG_N_SECTION_NAMES] = JDEBUG_SECTION_NAMES;
  state->string_table_size                    = 1; // starts with a '\0'
  for (size_t i = 0; i < JDEBUG_N_SECTION_NAMES; ++i) {
    state->string_indices[i] = state->string_table_size;
    state->string_table_size += strlen(strings[i]) + 1;
  }

  size_t data_cursor = state->data_start;
  jcontainer_ll_foreach(&state->mappings, mapping)
  {
    data_cursor = jdebug_round_up_power_of_2(data_cursor, state->page_size);
    data_cursor += jdebug_mapping_size(mapping);
  }

  state->notes_start      = jdebug_round_up_power_of_2(data_cursor, state->page_size);
  state->string_tab_start = data_cursor + state->notes_size;

  ret = 0;

done:
  close(auxv_fd);
  return ret;
}

static int jdebug_coredump_write_headers(coredump_state_t* state)
{
  size_t data_cursor;

  {
    Elf64_Ehdr elf_header          = {0};
    elf_header.e_ident[EI_MAG0]    = ELFMAG0;
    elf_header.e_ident[EI_MAG1]    = ELFMAG1;
    elf_header.e_ident[EI_MAG2]    = ELFMAG2;
    elf_header.e_ident[EI_MAG3]    = ELFMAG3;
    elf_header.e_ident[EI_CLASS]   = ELFCLASS64;
    elf_header.e_ident[EI_DATA]    = ELFDATA2LSB;
    elf_header.e_ident[EI_VERSION] = 1;
    elf_header.e_ident[EI_OSABI]   = ELFOSABI_SYSV;
    elf_header.e_type              = ET_CORE;
    elf_header.e_machine           = EM_X86_64;
    elf_header.e_version           = 1;
    elf_header.e_entry             = 0;
    elf_header.e_phoff             = state->phdr_start;
    elf_header.e_shoff             = state->shdr_start;
    elf_header.e_flags             = 0;
    elf_header.e_ehsize            = sizeof(Elf64_Ehdr);
    elf_header.e_phentsize         = sizeof(Elf64_Phdr);
    elf_header.e_phnum             = state->n_phdr;
    elf_header.e_shentsize         = sizeof(Elf64_Shdr);
    elf_header.e_shnum             = state->n_shdr;
    elf_header.e_shstrndx          = state->n_shdr - 1; // last section header

    if (-1 == jdebug_coredump_do_write(state, &elf_header, sizeof(elf_header))) {
      return -1;
    }
  }

  // program header for NOTES
  {
    Elf64_Phdr phdr;
    phdr.p_type   = PT_NOTE;
    phdr.p_flags  = PF_R;
    phdr.p_offset = state->notes_start;
    phdr.p_vaddr  = 0;
    phdr.p_paddr  = 0;
    phdr.p_filesz = state->notes_size;
    phdr.p_memsz  = 0;
    phdr.p_align  = 1;

    if (-1 == jdebug_coredump_do_write(state, &phdr, sizeof(phdr))) {
      return -1;
    }
  }

  // program headers for mappings
  data_cursor = state->data_start;
  jcontainer_ll_foreach(&state->mappings, mapping)
  {
    data_cursor = jdebug_round_up_power_of_2(data_cursor, state->page_size);
    Elf64_Phdr phdr;
    phdr.p_type  = PT_LOAD;
    phdr.p_flags = PF_R;
    if (mapping->access & MAPPING_ACCESS_WRITE)
      phdr.p_flags |= PF_W;
    if (mapping->access & MAPPING_ACCESS_READ)
      phdr.p_flags |= PF_X;
    phdr.p_offset = data_cursor;
    phdr.p_vaddr  = mapping->s_addr;
    phdr.p_paddr  = 0;

    const size_t mapping_size = jdebug_mapping_size(mapping);
    phdr.p_filesz             = mapping_size;
    phdr.p_memsz              = mapping_size;
    phdr.p_align              = 1;
    data_cursor += mapping_size;

    if (-1 == jdebug_coredump_do_write(state, &phdr, sizeof(phdr)))
      return -1;
  }

  // section header for NOTES
  {
    Elf64_Shdr shdr;
    shdr.sh_name      = state->string_indices[JDEBUG_SECTION_NAME_NOTE_INDEX];
    shdr.sh_type      = SHT_NOTE;
    shdr.sh_flags     = SHF_ALLOC;
    shdr.sh_addr      = 0;
    shdr.sh_offset    = state->notes_start;
    shdr.sh_size      = state->notes_size;
    shdr.sh_link      = 0;
    shdr.sh_info      = 0;
    shdr.sh_addralign = 1;
    shdr.sh_entsize   = 0;

    if (-1 == jdebug_coredump_do_write(state, &shdr, sizeof(shdr)))
      return -1;
  }

  // section headers for mappings
  data_cursor = state->data_start;
  jcontainer_ll_foreach(&state->mappings, mapping)
  {
    data_cursor = jdebug_round_up_power_of_2(data_cursor, state->page_size);
    Elf64_Shdr shdr;
    shdr.sh_name  = state->string_indices[JDEBUG_SECTION_NAME_LOAD_INDEX];
    shdr.sh_type  = SHT_PROGBITS;
    shdr.sh_flags = SHF_ALLOC;
    if (mapping->access & MAPPING_ACCESS_WRITE)
      shdr.sh_flags |= SHF_WRITE;
    if (mapping->access & MAPPING_ACCESS_EXEC)
      shdr.sh_flags |= SHF_EXECINSTR;
    shdr.sh_addr   = mapping->s_addr;
    shdr.sh_offset = data_cursor;

    const size_t mapping_size = jdebug_mapping_size(mapping);
    shdr.sh_size              = mapping_size;
    shdr.sh_link              = 0;
    shdr.sh_info              = 0;
    shdr.sh_addralign         = 1;
    shdr.sh_entsize           = 0;
    data_cursor += mapping_size;

    if (-1 == jdebug_coredump_do_write(state, &shdr, sizeof(shdr)))
      return -1;
  }

  // section header for string table
  {
    Elf64_Shdr shdr;
    shdr.sh_name      = state->string_indices[JDEBUG_SECTION_NAME_SHSTRTAB_INDEX];
    shdr.sh_type      = SHT_STRTAB;
    shdr.sh_flags     = 0;
    shdr.sh_addr      = 0;
    shdr.sh_offset    = state->string_tab_start;
    shdr.sh_size      = state->string_table_size;
    shdr.sh_link      = 0;
    shdr.sh_info      = 0;
    shdr.sh_addralign = 1;
    shdr.sh_entsize   = 0;

    if (-1 == jdebug_coredump_do_write(state, &shdr, sizeof(shdr)))
      return -1;
  }

  return 0;
}

static int jdebug_coredump_write_data_sections(coredump_state_t* state)
{
  int ret = -1;
  int vm_pipe[2];
  size_t data_cursor;

  if (-1 == pipe(vm_pipe)) {
    return -1;
  }

  data_cursor = state->data_start;
  jcontainer_ll_foreach(&state->mappings, mapping)
  {
    data_cursor               = jdebug_round_up_power_of_2(data_cursor, state->page_size);
    const size_t mapping_size = jdebug_mapping_size(mapping);
    uint8_t* buf              = malloc(mapping_size);

    const struct iovec local  = {buf, mapping_size};
    const struct iovec remote = {(void*) mapping->s_addr, mapping_size};

    ssize_t r = process_vm_readv(state->process, &local, 1, &remote, 1, 0);
    if (-1 == r)
      memset(buf, 0, mapping_size);

    if (jdebug_coredump_seek(state, (long) data_cursor)) {
      free(buf);
      goto done;
    }

    if (jdebug_coredump_do_write(state, buf, mapping_size)) {
      free(buf);
      goto done;
    }
    free(buf);

    data_cursor += mapping_size;
  }

  ret = 0;
done:
  close(vm_pipe[0]);
  close(vm_pipe[1]);
  return ret;
}

static int jdebug_coredump_write_notes(coredump_state_t* state)
{
  if (jdebug_coredump_seek(state, (long) state->notes_start))
    return -1;

  if (jdebug_coredump_write_prpsinfo(state))
    return -1;

  if (jdebug_coredump_write_prstatus(state))
    return -1;

  if (jdebug_coredump_write_auxv(state))
    return -1;

  return 0;
}

static int jdebug_coredump_write_prpsinfo(coredump_state_t* state)
{
  int ret = -1;

  Elf64_Nhdr nhdr;
  nhdr.n_namesz = 5;
  nhdr.n_descsz = sizeof(struct elf_prpsinfo);
  nhdr.n_type   = NT_PRPSINFO;
  if (-1 == jdebug_coredump_do_write(state, &nhdr, sizeof(nhdr)))
    goto done;

  if (-1 == jdebug_coredump_do_write(state, "CORE\0\0\0", 8))
    goto done;

  struct elf_prpsinfo info = {0};
  info.pr_sname            = 'R';
  info.pr_nice             = (char) getpriority(PRIO_PROCESS, state->process);
  info.pr_uid              = geteuid();
  info.pr_gid              = getegid();
  info.pr_pid              = state->process;

  char* status_path = NULL;
  if (asprintf(&status_path, "/proc/%d/status", state->process) < 0 || !status_path)
    goto done;

  FILE* status_fp = fopen(status_path, "r");
  free(status_path);
  if (!status_fp)
    goto done;

  char* line    = NULL;
  size_t line_n = 0;

  while (getline(&line, &line_n, status_fp) > 0) {
    char* c;
    if ((c = strstr(line, "PPid:"))) {
      info.pr_ppid = (int) strtol(c, NULL, 10);
    } else if ((c = strstr(line, "Tgid:"))) {
      info.pr_pgrp = (int) strtol(c, NULL, 10);
    }
  }

  free(line);
  fclose(status_fp);

  info.pr_sid = getsid(state->process);

  if (-1 == jdebug_coredump_do_write(state, &info, sizeof(info)))
    goto done;

  ret = 0;

done:
  return ret;
}

static int jdebug_coredump_write_prstatus(coredump_state_t* state)
{
  int ret = -1;

  const int64_t clock_tick_per_sec = sysconf(_SC_CLK_TCK);
  const int64_t ms_per_sec         = 1000 * 1000;

  for (size_t task_i = 0; task_i < state->n_tasks; ++task_i) {
    const pid_t task = state->tasks[task_i];
    Elf64_Nhdr nhdr;
    nhdr.n_namesz = 5;
    nhdr.n_descsz = sizeof(struct elf_prstatus);
    nhdr.n_type   = NT_PRSTATUS;
    if (-1 == jdebug_coredump_do_write(state, &nhdr, sizeof(nhdr)))
      goto done;

    if (-1 == jdebug_coredump_do_write(state, "CORE\0\0\0", 8))
      goto done;

    struct elf_prstatus status = {0};
    status.pr_info.si_signo    = state->siginfo->si_signo;
    status.pr_info.si_code     = state->siginfo->si_code;
    status.pr_info.si_errno    = state->siginfo->si_errno;
    status.pr_cursig           = state->siginfo->si_signo;
    status.pr_pid              = task;
    status.pr_sid              = getsid(state->process);

    char* status_path = NULL;
    if (asprintf(&status_path, "/proc/%d/task/%d/status", state->process, task) < 0 || !status_path)
      goto done;

    FILE* status_fp = fopen(status_path, "r");
    free(status_path);
    status_path = NULL;

    if (!status_fp)
      goto done;

    char* line    = NULL;
    size_t line_n = 0;

    while (getline(&line, &line_n, status_fp) > 0) {
      char* c;
      if ((c = strstr(line, "PPid:"))) {
        status.pr_ppid = (int) strtol(c, NULL, 10);
      } else if ((c = strstr(line, "Tgid:"))) {
        status.pr_pgrp = (int) strtol(c, NULL, 10);
      }
    }

    fclose(status_fp);
    free(line);
    line = NULL;

    if (asprintf(&status_path, "/proc/%d/task/%d/stat", state->process, task) < 0 || !status_path)
      goto done;

    status_fp = fopen(status_path, "r");
    free(status_path);
    status_path = NULL;

    if (!status_fp)
      goto done;

    if (getline(&line, &line_n, status_fp) > 0) {
      char *sv, *field;
      size_t field_i = 0;
      int64_t ms;
      for (field = strtok_r(line, " ", &sv);
           field;
           ++field_i, field = strtok_r(NULL, "", &sv)) {
        switch (field_i) {
          case 14: // utime
            ms                      = ms_per_sec * strtol(field, NULL, 10) / clock_tick_per_sec;
            status.pr_utime.tv_sec  = ms / ms_per_sec;
            status.pr_utime.tv_usec = ms % ms_per_sec;
            break;
          case 15: // stime
            ms                      = ms_per_sec * strtol(field, NULL, 10) / clock_tick_per_sec;
            status.pr_stime.tv_sec  = ms / ms_per_sec;
            status.pr_stime.tv_usec = ms % ms_per_sec;
            break;
          case 16: // cutime
            ms                       = ms_per_sec * strtol(field, NULL, 10) / clock_tick_per_sec;
            status.pr_cutime.tv_sec  = ms / ms_per_sec;
            status.pr_cutime.tv_usec = ms % ms_per_sec;
            break;
          case 17: // cstime
            ms                       = ms_per_sec * strtol(field, NULL, 10) / clock_tick_per_sec;
            status.pr_cstime.tv_sec  = ms / ms_per_sec;
            status.pr_cstime.tv_usec = ms % ms_per_sec;
            break;
          case 31: // signal
            status.pr_sigpend = strtoul(field, NULL, 10);
            break;
          case 32: // blocked
            status.pr_sighold = strtoul(field, NULL, 10);
            break;
          default:
            break;
        }
      }
    }

    struct user_regs_struct regs;

    if (-1 == ptrace(PTRACE_GETREGS, task, &regs, &regs))
      goto done;

    if (task == state->crash_task) {
      regs.r15      = state->ucontext->uc_mcontext.gregs[REG_R15];
      regs.r14      = state->ucontext->uc_mcontext.gregs[REG_R14];
      regs.r13      = state->ucontext->uc_mcontext.gregs[REG_R13];
      regs.r12      = state->ucontext->uc_mcontext.gregs[REG_R12];
      regs.rbp      = state->ucontext->uc_mcontext.gregs[REG_RBP];
      regs.rbx      = state->ucontext->uc_mcontext.gregs[REG_RBX];
      regs.r11      = state->ucontext->uc_mcontext.gregs[REG_R11];
      regs.r10      = state->ucontext->uc_mcontext.gregs[REG_R10];
      regs.r9       = state->ucontext->uc_mcontext.gregs[REG_R9];
      regs.r8       = state->ucontext->uc_mcontext.gregs[REG_R8];
      regs.rax      = state->ucontext->uc_mcontext.gregs[REG_RAX];
      regs.rcx      = state->ucontext->uc_mcontext.gregs[REG_RCX];
      regs.rsi      = state->ucontext->uc_mcontext.gregs[REG_RSI];
      regs.rdi      = state->ucontext->uc_mcontext.gregs[REG_RDI];
      regs.orig_rax = -1;
      regs.rip      = state->ucontext->uc_mcontext.gregs[REG_RIP];
      regs.rsp      = state->ucontext->uc_mcontext.gregs[REG_RSP];
    }
    memcpy(&status.pr_reg, &regs, sizeof(status.pr_reg));

    status.pr_fpvalid = 1;
    if (-1 == jdebug_coredump_do_write(state, &status, sizeof(status)))
      goto done;

    nhdr.n_descsz = sizeof(prfpregset_t);
    nhdr.n_type   = NT_PRFPREG;
    if (-1 == jdebug_coredump_do_write(state, &nhdr, sizeof(nhdr)))
      goto done;

    if (-1 == jdebug_coredump_do_write(state, "CORE\0\0\0", 8))
      goto done;

    prfpregset_t fpregs;
    if (-1 == ptrace(PTRACE_GETFPREGS, task, &fpregs, &fpregs))
      goto done;

    if (-1 == jdebug_coredump_do_write(state, &fpregs, sizeof(fpregs)))
      goto done;
  }

  ret = 0;

done:
  return ret;
}

static int jdebug_coredump_write_auxv(coredump_state_t* state)
{
  int ret     = -1;
  int auxv_fd = -1;

  Elf64_Nhdr nhdr;
  nhdr.n_namesz = 5;
  nhdr.n_descsz = state->n_auxv * sizeof(Elf64_auxv_t);
  nhdr.n_type   = NT_AUXV;

  if (-1 == jdebug_coredump_do_write(state, &nhdr, sizeof(nhdr))) {
    perror("write1");
    goto done;
  }

  if (-1 == jdebug_coredump_do_write(state, "CORE\0\0\0", 8)) {
    perror("write2");
    goto done;
  }

  auxv_fd = open(state->auxv_path, O_RDONLY);
  for (size_t i = 0; i < state->n_auxv; ++i) {
    Elf64_auxv_t aux;
    if (-1 == jdebug_read_all(auxv_fd, &aux, sizeof(aux), -1)) {
      perror("read1");
      goto done;
    }

    if (-1 == jdebug_coredump_do_write(state, &aux, sizeof(aux)))
      goto done;
  }

  ret = 0;

done:
  if (-1 != auxv_fd)
    close(auxv_fd);
  return ret;
}

static int jdebug_coredump_write_string_table(coredump_state_t* state)
{
  if (jdebug_coredump_seek(state, (long) state->string_tab_start))
    return -1;

  if (-1 == jdebug_coredump_do_write(state, "\0", 1))
    return -1;

  const char* strings[] = JDEBUG_SECTION_NAMES;
  for (size_t string_i = 0; string_i < JDEBUG_N_SECTION_NAMES; ++string_i) {
    const char* str = strings[string_i];
    if (-1 == jdebug_coredump_do_write(state, str, strlen(str) + 1))
      return -1;
  }

  return 0;
}

static int jdebug_coredump_seek(coredump_state_t* state, size_t pos)
{
  if (pos < state->n_wrote) {
    fprintf(stderr, "invalid seek\n");
    return -1;
  }

  int ret = 0;

  while (state->n_wrote < pos) {
    const char buf[1024] = {0};
    size_t n             = pos - state->n_wrote;
    if (n > sizeof(buf))
      n = sizeof(buf);

    ssize_t wrote = jdebug_write_all(state->output_fd, buf, n);
    if (-1 == wrote) {
      if (errno == EINTR)
        continue;
      perror("splice");
      ret = -1;
      break;
    }
    state->n_wrote += wrote;
  }

  return ret;
}

static int jdebug_coredump_do_write(coredump_state_t* state, const void* buf, size_t len)
{
  ssize_t wrote = jdebug_write_all(state->output_fd, buf, len);
  if (-1 == wrote) {
    return -1;
  }
  state->n_wrote += wrote;
  return (size_t) wrote == len ? 0 : -1;
}

static const char* jdebug_coredump_compressor_prog(compressor_t compressor)
{
  switch (compressor) {
    case JDEBUG_COREDUMP_COMPRESSOR_ZSTD:
      return "zstd";
    case JDEBUG_COREDUMP_COMPRESSOR_XZ:
      return "xz";
    case JDEBUG_COREDUMP_COMPRESSOR_BZIP2:
      return "bzip2";
    case JDEBUG_COREDUMP_COMPRESSOR_GZIP:
      return "gzip";
    default:
      break;
  }
  return NULL;
}

static const char* jdebug_coredump_compressor_ext(compressor_t compressor)
{
  switch (compressor) {
    case JDEBUG_COREDUMP_COMPRESSOR_ZSTD:
      return "zst";
    case JDEBUG_COREDUMP_COMPRESSOR_XZ:
      return "xz";
    case JDEBUG_COREDUMP_COMPRESSOR_BZIP2:
      return "bz2";
    case JDEBUG_COREDUMP_COMPRESSOR_GZIP:
      return "gz";
    default:
      break;
  }
  return NULL;
}

static compressor_t jdebug_coredump_choose_compressor()
{
  compressor_t compressor = JDEBUG_COREDUMP_COMPRESSOR_ZSTD;
  do {
    pid_t child = fork();
    if (0 == child) {
      const char* prog   = jdebug_coredump_compressor_prog(compressor);
      const char* args[] = {prog, "-V", NULL};

      int devnull = open("/dev/null", O_WRONLY);
      dup2(devnull, STDOUT_FILENO);
      close(devnull);

      execvp(prog, (char* const*) args);
      _Exit(errno);
    }
    if (child < 0)
      continue;
    int wstatus = 0;
    if (!waitpid(child, &wstatus, 0))
      continue;
    if (!WEXITSTATUS(wstatus))
      return compressor;
  } while (++compressor <= JDEBUG_COREDUMP_COMPRESSOR_LAST);
  return JDEBUG_COREDUMP_COMPRESSOR_NONE;
}

static int jdebug_coredump_open_compressor(coredump_state_t* state)
{
  const char* prog = jdebug_coredump_compressor_prog(state->compressor);
  if (!prog) {
    state->output_fd = open(state->filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    return state->output_fd != -1 ? 0 : -1;
  }

  int output_fd[2];
  if (-1 == pipe(output_fd))
    return -1;

  pid_t compressor_pid = fork();
  if (0 == compressor_pid) {
    const char* args[] = {prog, "-", NULL};

    char filename[PATH_MAX + 1];
    snprintf(filename, sizeof(filename), "%s.%s", state->filename, jdebug_coredump_compressor_ext(state->compressor));

    int output = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (-1 == output)
      _Exit(errno);

    dup2(output_fd[0], STDIN_FILENO);
    close(output_fd[0]);
    close(output_fd[1]);

    dup2(output, STDOUT_FILENO);
    close(output);

    execvp(prog, (char* const*) args);
    _Exit(errno);
  }

  close(output_fd[0]);

  if (compressor_pid < 0) {
    close(output_fd[1]);
    return -1;
  }

  state->output_fd      = output_fd[1];
  state->compressor_pid = compressor_pid;

  return 0;
}

static int jdebug_coredump_close_compressor(coredump_state_t* state)
{
  if (state->output_fd > 0)
    close(state->output_fd);

  if (state->compressor_pid <= 0)
    return -1;

  int wstatus = 0;
  int r;
  do {
    r = waitpid(state->compressor_pid, &wstatus, 0);
  } while (-1 == r && errno != EINTR);
  if (-1 == r)
    return -1;
  return WEXITSTATUS(wstatus) == 0 ? 0 : -1;
}
