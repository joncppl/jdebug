#include "jdebug_priv.h"

#include <stdlib.h>
#include <string.h>

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <libdwarf/dwarf.h>
#include <libdwarf/libdwarf.h>

static int jdebug_get_load_bias(mapping_t* mapping);

mapping_t* jdebug_mappings_load(pid_t pid)
{
  mapping_t* mappings = NULL;
  char* map_path      = NULL;
  char* line          = NULL;
  size_t line_n       = 0;

  if (-1 == asprintf(&map_path, "/proc/%d/maps", pid)) {
    map_path = NULL;
    goto error;
  }

  FILE* map_file = fopen(map_path, "r");
  if (!map_file)
    goto error;

  while (getline(&line, &line_n, map_file) > 0) {
    mapping_t mapping = {0};
    char* cursor      = line;
    char* end         = NULL;
    char access_buf[5];
    size_t l;

    mapping.s_addr = strtoull(cursor, &end, 16);
    if (end == cursor || *end != '-')
      continue;
    cursor = end + 1;

    mapping.e_addr = strtoull(cursor, &end, 16);
    if (end == cursor || *end != ' ')
      continue;
    cursor = end + 1;

    l = strlen(line);
    if (l < 4)
      continue;

    strncpy(access_buf, cursor, 4);
    cursor += 4;

    mapping.offset = strtoull(cursor, &end, 16);
    if (end == cursor || *end != ' ')
      continue;
    cursor = end + 1;

    // dev_major
    strtoul(cursor, &end, 16);
    if (end == cursor || *end != ':')
      continue;
    cursor = end + 1;

    // dev_minor
    strtoul(cursor, &end, 16);
    if (end == cursor || *end != ' ')
      continue;
    cursor = end + 1;

    // inode
    strtoull(cursor, &end, 10);
    if (end == cursor)
      continue;
    cursor = end;

    // skip spaces
    while (*cursor == ' ' && *cursor != '\0') {
      ++cursor;
    }

    size_t pathname_len = strlen(cursor);

    if (pathname_len) {
      mapping.pathname = strdup(cursor);
      if (!mapping.pathname)
        goto error;

      size_t pathname_end = pathname_len - 1;
      if (mapping.pathname[pathname_end] == '\n')
        mapping.pathname[pathname_end] = '\0';
    } else {
      mapping.pathname = NULL;
    }


    mapping.access = 0;
    if (strchr(access_buf, 'r'))
      mapping.access |= MAPPING_ACCESS_READ;
    if (strchr(access_buf, 'w'))
      mapping.access |= MAPPING_ACCESS_WRITE;
    if (strchr(access_buf, 'x'))
      mapping.access |= MAPPING_ACCESS_EXEC;
    if (strchr(access_buf, 's'))
      mapping.access |= MAPPING_ACCESS_SHARE;
    if (strchr(access_buf, 'p'))
      mapping.access |= MAPPING_ACCESS_PRIV;

    if (mapping.pathname && pathname_len > 1) {
      if (jdebug_get_load_bias(&mapping)) {
        mapping.load_bias = 0;
      }
    }

    if (jcontainer_ll_push_back(&mappings, &mapping))
      goto error;
  }

  goto done;

error:
  jdebug_mappings_free(mappings);

done:
  free(line);
  free(map_file);
  return mappings;
}

JCONTAINER_DECLARE_ARRAY(Elf64_Phdr, phdr_list_t)

static int jdebug_cmp_load_segment(const void* p1, const void* p2)
{
  const Elf64_Phdr* phdr1 = p1;
  const Elf64_Phdr* phdr2 = p2;

  Elf64_Off o1 = phdr1->p_offset;
  Elf64_Off o2 = phdr2->p_offset;

  if (o1 == o2)
    return 0;
  return o1 < o2 ? -1 : 1;
}

static int jdebug_get_load_bias(mapping_t* mapping)
{
  int ret               = -1;
  long page_size        = 0;
  Elf64_Ehdr elf_header = {0};
  phdr_list_t phdr_list = jcontainer_array_init;

  int fd = open(mapping->pathname, O_RDONLY);
  if (-1 == fd)
    goto done;

  if (-1 == (page_size = sysconf(_SC_PAGESIZE)))
    goto done;

  if (jdebug_read_all(fd, &elf_header, sizeof(elf_header), JDEBUG_DEFAULT_TIMEOUT_MS) != sizeof(elf_header))
    goto done;


  for (Elf64_Half i = 0; i < elf_header.e_phnum; ++i) {
    Elf64_Phdr phdr = {0};

    if (-1 == lseek(fd, (off_t) (elf_header.e_phoff + elf_header.e_phentsize * i), SEEK_SET))
      goto done;

    if (jdebug_read_all(fd, &phdr, sizeof(phdr), JDEBUG_DEFAULT_TIMEOUT_MS) != sizeof(phdr))
      goto done;

    if (phdr.p_type != PT_LOAD)
      continue;

    if (jcontainer_array_push_back(&phdr_list, &phdr))
      goto done;
  }

  qsort(phdr_list.base, phdr_list.size, sizeof(*phdr_list.base), jdebug_cmp_load_segment);

  for (size_t i = 0; i < phdr_list.size; ++i) {
    Elf64_Phdr* phdr = &phdr_list.base[i];

    if (phdr->p_offset >= mapping->offset) {
      mapping->load_bias = mapping->s_addr - jdebug_round_down_power_of_2(phdr->p_vaddr, page_size);
      ret                = 0;
      break;
    }
  }

done:
  jcontainer_array_fini(&phdr_list);
  if (-1 != fd)
    close(fd);
  return ret;
}

void jdebug_mappings_free(mapping_t* mappings)
{
  jcontainer_ll_foreach(&mappings, mapping)
  {
    free(mapping->pathname);
  }
  jcontainer_ll_clear(&mappings);
}

size_t jdebug_mapping_size(const mapping_t* mapping)
{
  return mapping->e_addr - mapping->s_addr;
}

size_t jdebug_mapping_memory_to_object(const mapping_t* mapping, const size_t memory_addr)
{
  if (mapping->load_bias > 0)
    return memory_addr - mapping->load_bias;
  else
    return memory_addr - mapping->s_addr + mapping->offset;
}

const mapping_t* jdebug_mapping_lookup(const mapping_t* mappings, const size_t addr)
{
  jcontainer_ll_foreach(&mappings, mapping)
  {
    if (addr >= mapping->s_addr && addr <= mapping->e_addr)
      return mapping;
  }

  return NULL;
}

void jdebug_mapping_print(const mapping_t* mapping, const logger_t* log)
{
  jdebug_log_format(log, "  0x%zX - 0x%zX [0x%zX] %s\n", mapping->s_addr, mapping->e_addr, mapping->offset, mapping->pathname);
}

static int jdebug_mapping_get_source_lines_die(const Dwarf_Debug dbg,
                                               const Dwarf_Die die,
                                               const size_t target_addr,
                                               char** source_file,
                                               uint64_t* source_line)
{
  Dwarf_Error err         = NULL;
  Dwarf_Unsigned version  = 0;
  Dwarf_Small table_count = 0;
  Dwarf_Line_Context ctx  = NULL;
  Dwarf_Line* lines       = NULL;
  Dwarf_Signed line_count = 0;
  Dwarf_Line best         = NULL;
  Dwarf_Addr best_addr    = 0;
  char* src               = NULL;
  Dwarf_Unsigned lineno   = 0;
  int ret                 = -1;

  if (dwarf_srclines_b(die, &version, &table_count, &ctx, &err)
      || dwarf_srclines_from_linecontext(ctx, &lines, &line_count, &err))
    goto done;

  for (Dwarf_Signed i = 0; i < line_count; i++) {
    Dwarf_Addr row_addr     = 0;
    Dwarf_Bool end_sequence = 0;

    if (dwarf_lineaddr(lines[i], &row_addr, &err))
      continue;

    dwarf_lineendsequence(lines[i], &end_sequence, &err);
    if (end_sequence)
      continue;

    if (dwarf_linesrc(lines[i], &src, &err)
        || dwarf_lineno(lines[i], &lineno, &err))
      goto done;

    if (row_addr <= target_addr && (!best || row_addr > best_addr)) {
      best      = lines[i];
      best_addr = row_addr;
    }
  }

  Dwarf_Addr row_addr = 0;
  if (!best
      || dwarf_lineaddr(best, &row_addr, &err)
      || dwarf_linesrc(best, &src, &err)
      || dwarf_lineno(best, &lineno, &err))
    goto done;

  *source_file = strdup(src);
  if (!*source_file)
    goto done;

  *source_line = lineno;
  ret          = 1;

done:
  if (err)
    dwarf_dealloc_error(dbg, err);
  if (src)
    dwarf_dealloc(dbg, src, DW_DLA_STRING);
  dwarf_srclines_dealloc_b(ctx);
  return ret;
}

static int jdebug_dwarf_die_in_low_high_pc(const Dwarf_Debug dbg,
                                           const Dwarf_Die die,
                                           const size_t target_addr)
{
  Dwarf_Error err             = NULL;
  Dwarf_Addr lowpc            = 0;
  Dwarf_Addr highpc           = 0;
  Dwarf_Half form             = 0;
  enum Dwarf_Form_Class class = 0;
  int ret                     = 0;

  if (dwarf_lowpc(die, &lowpc, &err)
      || dwarf_highpc_b(die, &highpc, &form, &class, &err))
    goto done;

  if (class == DW_FORM_CLASS_CONSTANT)
    highpc += lowpc;

  ret = lowpc <= target_addr && target_addr < highpc;

done:
  if (err)
    dwarf_dealloc_error(dbg, err);
  return ret;
}

static int jdebug_dwarf_die_in_ranges(const Dwarf_Debug dbg,
                                      const Dwarf_Die die,
                                      const size_t target_addr)
{
  Dwarf_Error err              = NULL;
  Dwarf_Attribute attr         = NULL;
  Dwarf_Half form              = 0;
  Dwarf_Unsigned ranges_offset = 0;
  Dwarf_Ranges* ranges         = NULL;
  Dwarf_Signed range_count     = 0;
  Dwarf_Unsigned byte_count    = 0;
  Dwarf_Off real_offset        = 0;
  Dwarf_Bool have_base         = 0;
  Dwarf_Unsigned base          = 0;
  int ret                      = 0;

  if (dwarf_attr(die, DW_AT_ranges, &attr, &err)
      || dwarf_whatform(attr, &form, &err))
    goto done;

  if (form == DW_FORM_sec_offset) {
    if (dwarf_global_formref(attr, &ranges_offset, &err))
      goto done;
  } else if (dwarf_formudata(attr, &ranges_offset, &err)) {
    goto done;
  }

  if (dwarf_get_ranges_b(
        dbg,
        ranges_offset,
        die,
        &real_offset,
        &ranges,
        &range_count,
        &byte_count,
        &err))
    goto done;

  for (Dwarf_Signed i = 0; i < range_count; i++) {
    Dwarf_Ranges* r = &ranges[i];

    switch (r->dwr_type) {
      case DW_RANGES_ENTRY: {
        Dwarf_Addr lo = r->dwr_addr1;
        Dwarf_Addr hi = r->dwr_addr2;

        if (have_base) {
          lo += base;
          hi += base;
        }

        if (lo <= target_addr && target_addr < hi)
          ret = 1;
        break;
      }

      case DW_RANGES_ADDRESS_SELECTION:
        base      = r->dwr_addr2;
        have_base = 1;
        break;

      case DW_RANGES_END:
        break;
    }

    if (ret)
      break;
  }

done:
  if (err)
    dwarf_dealloc_error(dbg, err);
  if (attr)
    dwarf_dealloc_attribute(attr);
  if (ranges)
    dwarf_dealloc(dbg, ranges, DW_DLA_RANGES);
  return ret;
}

static int jdebug_dwarf_die_in_rnglists(const Dwarf_Debug dbg,
                                        const Dwarf_Die die,
                                        const size_t target_addr)
{
  Dwarf_Error err              = NULL;
  Dwarf_Attribute attr         = NULL;
  Dwarf_Half form              = 0;
  Dwarf_Unsigned ranges_offset = 0;
  Dwarf_Rnglists_Head head     = NULL;
  Dwarf_Unsigned count         = 0;
  Dwarf_Unsigned offset        = 0;
  int ret                      = 0;

  if (dwarf_attr(die, DW_AT_ranges, &attr, &err)
      || dwarf_whatform(attr, &form, &err)) {
    goto done;
  }

  if (form == DW_FORM_sec_offset) {
    if (dwarf_global_formref(attr, &ranges_offset, &err))
      goto done;
  } else if (dwarf_formudata(attr, &ranges_offset, &err)) {
    goto done;
  }

  if (dwarf_rnglists_get_rle_head(attr, form, ranges_offset, &head, &count, &offset, &err))
    goto done;

  for (Dwarf_Unsigned i = 0; i < count; i++) {
    unsigned entrylen                 = 0;
    unsigned code                     = 0;
    Dwarf_Unsigned rawlowpc           = 0;
    Dwarf_Unsigned rawhighpc          = 0;
    Dwarf_Bool debug_addr_unavailable = 0;
    Dwarf_Unsigned lowpc              = 0;
    Dwarf_Unsigned highpc             = 0;

    if (dwarf_get_rnglists_entry_fields_a(
          head,
          i,
          &entrylen,
          &code,
          &rawlowpc,
          &rawhighpc,
          &debug_addr_unavailable,
          &lowpc,
          &highpc,
          &err))
      goto done;

    if (debug_addr_unavailable)
      continue;

    if (lowpc <= target_addr && target_addr < highpc) {
      ret = 1;
      break;
    }
  }

done:
  if (err)
    dwarf_dealloc_error(dbg, err);
  if (head)
    dwarf_dealloc_rnglists_head(head);
  if (attr)
    dwarf_dealloc_attribute(attr);
  return ret;
}

static int jdebug_mapping_dwarf_die_in_range(const Dwarf_Debug dbg,
                                             const Dwarf_Die die,
                                             const size_t target_addr)
{
  return jdebug_dwarf_die_in_low_high_pc(dbg, die, target_addr)
         || jdebug_dwarf_die_in_ranges(dbg, die, target_addr)
         || jdebug_dwarf_die_in_rnglists(dbg, die, target_addr);
}

int jdebug_mapping_get_source_file(const mapping_t* mapping, size_t target_addr, char** source_file, uint64_t* source_line)
{
  Dwarf_Debug dbg    = NULL;
  Dwarf_Error err    = NULL;
  Dwarf_Half version = 0;
  int ret            = -1;

  *source_file = NULL;
  *source_line = 0;

  if (dwarf_init_path(mapping->pathname, NULL, 0, DW_GROUPNUMBER_ANY, NULL, NULL, &dbg, &err))
    goto done;

  while (!dwarf_next_cu_header_d(dbg,
                                 1,
                                 NULL,
                                 &version,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 &err)) {
    Dwarf_Die die = NULL;
    if (dwarf_siblingof_b(dbg, NULL, 1, &die, &err))
      continue;

    if (!jdebug_mapping_dwarf_die_in_range(dbg, die, target_addr)) {
      dwarf_dealloc_die(die);
      continue;
    }

    ret = jdebug_mapping_get_source_lines_die(dbg, die, target_addr, source_file, source_line);
    dwarf_dealloc_die(die);

    if (!ret)
      break;
  }

done:
  if (dbg) {
    if (err)
      dwarf_dealloc_error(dbg, err);
    dwarf_finish(dbg);
  }
  return ret;
}
