#include "symbol.h"

#ifdef JDEBUG_USE_LIBDWARF
#include <dwarf.h>
#include <libdwarf.h>
#endif

#include <stdio.h>

#ifdef JDEBUG_USE_LIBDWARF
static int jdebug_find_symbol_dwarf(IMAGEHLP_MODULE* module_info, size_t pc, symbol_info_t* result);
#endif

int jdebug_find_symbol(IMAGEHLP_MODULE* module_info, size_t pc, symbol_info_t* result)
{
  struct SymbolBuffer
  {
    SYMBOL_INFO symbol;
    char name_buf[1024];
  } symbol_buffer;
  ZeroMemory(&symbol_buffer, sizeof(symbol_buffer));
  symbol_buffer.symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
  symbol_buffer.symbol.MaxNameLen   = sizeof(symbol_buffer.name_buf);

  char undec_name_buf[1024];

  size_t displacement;
  if (SymFromAddr(GetCurrentProcess(), pc, &displacement,
                  &symbol_buffer.symbol)) {
    UnDecorateSymbolName(symbol_buffer.symbol.Name, undec_name_buf, sizeof(undec_name_buf), UNDNAME_COMPLETE);

    result->start_address = symbol_buffer.symbol.Address - module_info->BaseOfImage;
    result->displacement  = displacement;
    result->name          = _strdup(undec_name_buf);

    IMAGEHLP_LINE64 line;
    ZeroMemory(&line, sizeof(line));
    line.SizeOfStruct = sizeof(line);
    DWORD line_displacement;
    if (SymGetLineFromAddr64(GetCurrentProcess(), pc, &line_displacement, &line)) {
      result->filename = _strdup(line.FileName);
      result->line     = line.LineNumber;
    }
    return 1;
  }

#if JDEBUG_USE_LIBDWARF
  return jdebug_find_symbol_dwarf(module_info, pc, result);
#else
  return 0;
#endif
}

#ifdef JDEBUG_USE_LIBDWARF

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

static int jdebug_dwarf_die_in_range(const Dwarf_Debug dbg,
                                     const Dwarf_Die die,
                                     const size_t target_addr)
{
  return jdebug_dwarf_die_in_low_high_pc(dbg, die, target_addr)
         || jdebug_dwarf_die_in_rnglists(dbg, die, target_addr);
}

extern char* __cxa_demangle(const char*, char*, size_t*, int*) __attribute__((weak));

static int jdebug_dwarf_get_subprog_name(const Dwarf_Debug dbg,
                                         const Dwarf_Die die,
                                         char** name_out)
{
  Dwarf_Error err              = NULL;
  Dwarf_Bool has_name          = FALSE;
  Dwarf_Bool has_linkage_name  = FALSE;
  Dwarf_Bool has_specification = FALSE;
  Dwarf_Attribute attr         = NULL;
  Dwarf_Die spec               = NULL;
  char* name                   = NULL;
  int ret                      = 0;

  if (dwarf_hasattr(die, DW_AT_name, &has_name, &err)
      || dwarf_hasattr(die, DW_AT_linkage_name, &has_linkage_name, &err)
      || dwarf_hasattr(die, DW_AT_specification, &has_specification, &err))
    goto done;

  // TODO: add namespace

  if (has_linkage_name) {
    if (dwarf_attr(die, DW_AT_linkage_name, &attr, &err)
        || dwarf_formstring(attr, &name, &err))
      goto done;

  } else if (has_name) {
    if (dwarf_attr(die, DW_AT_name, &attr, &err)
        || dwarf_formstring(attr, &name, &err))
      goto done;

  } else if (has_specification) {
    Dwarf_Off offset = 0;
    Dwarf_Bool info  = FALSE;

    if (dwarf_attr(die, DW_AT_specification, &attr, &err)
        || dwarf_global_formref_b(attr, &offset, &info, &err))
      goto done;

    dwarf_dealloc_attribute(attr);
    attr = NULL;

    if (dwarf_offdie_b(dbg, offset, info, &spec, &err)
        || dwarf_hasattr(spec, DW_AT_linkage_name, &has_linkage_name, &err))
      goto done;

    if (has_linkage_name) {
      if (dwarf_attr(spec, DW_AT_linkage_name, &attr, &err))
        goto done;
    } else if (dwarf_attr(spec, DW_AT_name, &attr, &err)) {
      goto done;
    }

    if (dwarf_formstring(attr, &name, &err))
      goto done;
  } else {
    goto done;
  }

  char* temp = NULL;
  if (__cxa_demangle)
    temp = __cxa_demangle(name, NULL, NULL, NULL);
  if (!temp)
    temp = strdup(name);
  *name_out = temp;

  ret = 1;

done:
  if (err)
    dwarf_dealloc_error(dbg, err);
  if (spec)
    dwarf_dealloc_die(spec);
  if (attr)
    dwarf_dealloc_attribute(attr);

  return ret;
}

static int jdebug_dwarf_check_subprog(const Dwarf_Debug dbg,
                                      const Dwarf_Die die,
                                      const size_t target_addr,
                                      Dwarf_Addr* lo_out,
                                      char** name_out)
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

  if (lowpc <= target_addr && target_addr < highpc) {
    if (!jdebug_dwarf_get_subprog_name(dbg, die, name_out))
      goto done;
    *lo_out = lowpc;
    ret     = 1;
  }

done:
  if (err)
    dwarf_dealloc_error(dbg, err);

  return ret;
}

static int jdebug_dwarf_find_subprog(const Dwarf_Debug dbg,
                                     const Dwarf_Die die,
                                     const size_t target_addr,
                                     Dwarf_Addr* lo_out,
                                     char** name_out)
{
  Dwarf_Error err = NULL;
  Dwarf_Die child = NULL;
  int ret         = 0;

  if (dwarf_child(die, &child, &err))
    goto done;

  for (;;) {
    if ((ret = jdebug_dwarf_find_subprog(dbg, child, target_addr, lo_out, name_out)))
      goto done;

    Dwarf_Half tag = 0;
    dwarf_tag(child, &tag, &err);
    if (tag == DW_TAG_subprogram
        && ((ret = jdebug_dwarf_check_subprog(dbg, child, target_addr, lo_out, name_out))))
      goto done;

    Dwarf_Die next = NULL;
    if (dwarf_siblingof_c(child, &next, &err))
      break;

    dwarf_dealloc_die(child);
    child = next;
  }

done:
  if (err)
    dwarf_dealloc_error(dbg, err);
  if (die)
    dwarf_dealloc_die(child);
  return ret;
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

static int jdebug_find_symbol_dwarf(IMAGEHLP_MODULE* module_info,
                                    const size_t pc,
                                    symbol_info_t* result)
{
  Dwarf_Debug dbg    = NULL;
  Dwarf_Error err    = NULL;
  Dwarf_Half version = 0;
  int ret            = -1;

  const size_t target_addr = pc - module_info->BaseOfImage + 0x140000000 - 1;

  if (dwarf_init_path(module_info->ImageName, NULL, 0, DW_GROUPNUMBER_ANY, NULL, NULL, &dbg, &err))
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

    if (!jdebug_dwarf_die_in_range(dbg, die, target_addr)) {
      dwarf_dealloc_die(die);
      continue;
    }

    Dwarf_Addr prog_low = 0;
    char* prog_name     = NULL;
    ret                 = jdebug_dwarf_find_subprog(dbg, die, target_addr, &prog_low, &prog_name);
    if (!ret) {
      dwarf_dealloc_die(die);
      break;
    }

    result->start_address = prog_low - 0x140000000;
    result->displacement  = target_addr - prog_low;
    result->name          = prog_name;

    char* source_file    = NULL;
    uint64_t source_line = 0;
    if (jdebug_mapping_get_source_lines_die(dbg, die, target_addr, &source_file, &source_line)) {
      result->filename = source_file;
      result->line     = source_line;
    } else {
      result->filename = NULL;
      result->line     = 0;
    }

    dwarf_dealloc_die(die);
  }

done:
  if (dbg) {
    dwarf_dealloc_error(dbg, err);
    dwarf_finish(dbg);
  }
  return ret;
}

#endif
