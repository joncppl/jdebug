#include "symbol.h"

#ifdef JDEBUG_USE_LIBDWARF
#include <dwarf.h>
#include <libdwarf.h>
#endif

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
static int jdebug_dwarf_die_in_low_high_pc(Dwarf_Die die, size_t target_addr, Dwarf_Error err)
{
  Dwarf_Addr lowpc            = 0;
  Dwarf_Addr highpc           = 0;
  Dwarf_Half form             = 0;
  enum Dwarf_Form_Class class = 0;

  if (dwarf_lowpc(die, &lowpc, &err) != DW_DLV_OK)
    return 0;

  if (dwarf_highpc_b(die, &highpc, &form, &class, &err) != DW_DLV_OK)
    return 0;

  if (class == DW_FORM_CLASS_CONSTANT)
    highpc += lowpc;

  return lowpc <= target_addr && target_addr < highpc;
}

static int jdebug_dwarf_die_in_rnglists(Dwarf_Die die, size_t target_addr, Dwarf_Error err)
{
  int r                        = 0;
  Dwarf_Attribute attr         = NULL;
  Dwarf_Half form              = 0;
  Dwarf_Unsigned ranges_offset = 0;
  Dwarf_Rnglists_Head head     = NULL;
  Dwarf_Unsigned count         = 0;
  Dwarf_Unsigned offset        = 0;

  if (dwarf_attr(die, DW_AT_ranges, &attr, &err) != DW_DLV_OK) {
    goto done;
  }

  if (dwarf_whatform(attr, &form, &err) != DW_DLV_OK)
    goto done;

  if (form == DW_FORM_sec_offset) {
    if (dwarf_global_formref(attr, &ranges_offset, &err) != DW_DLV_OK)
      goto done;
  } else {
    if (dwarf_formudata(attr, &ranges_offset, &err) != DW_DLV_OK)
      goto done;
  }

  if (dwarf_rnglists_get_rle_head(attr, form, ranges_offset, &head, &count, &offset, &err) != DW_DLV_OK)
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
          &err)
        != DW_DLV_OK)
      goto done;

    if (debug_addr_unavailable)
      continue;

    if (lowpc <= target_addr && target_addr < highpc) {
      r = 1;
      break;
    }
  }
done:
  if (head)
    dwarf_dealloc_rnglists_head(head);
  if (attr)
    dwarf_dealloc_attribute(attr);
  return r;
}

static int jdebug_dwarf_die_in_range(Dwarf_Die die, size_t target_addr, Dwarf_Error err)
{
  if (jdebug_dwarf_die_in_low_high_pc(die, target_addr, err))
    return 1;

  if (jdebug_dwarf_die_in_rnglists(die, target_addr, err))
    return 1;

  return 0;
}

static int jdebug_dwarf_check_subprog(Dwarf_Die die, size_t target_addr, Dwarf_Error err, Dwarf_Addr* low_out, char** name_out)
{
  Dwarf_Addr lowpc            = 0;
  Dwarf_Addr highpc           = 0;
  Dwarf_Half form             = 0;
  enum Dwarf_Form_Class class = 0;

  if (dwarf_lowpc(die, &lowpc, &err) != DW_DLV_OK)
    return 0;

  if (dwarf_highpc_b(die, &highpc, &form, &class, &err) != DW_DLV_OK)
    return 0;

  if (class == DW_FORM_CLASS_CONSTANT)
    highpc += lowpc;

  if (lowpc <= target_addr && target_addr < highpc) {
    Dwarf_Attribute attr = NULL;
    char* name           = NULL;
    if (dwarf_attr(die, DW_AT_name, &attr, &err) != DW_DLV_OK)
      return 0;

    if (dwarf_formstring(attr, &name, &err) != DW_DLV_OK) {
      dwarf_dealloc_attribute(attr);
      return 0;
    }

    *low_out  = lowpc;
    *name_out = strdup(name);

    dwarf_dealloc_attribute(attr);
    return 1;
  }

  return 0;
}

static int jdebug_dwarf_find_subprog(Dwarf_Debug dbg, Dwarf_Die die, size_t target_addr, Dwarf_Error err, Dwarf_Addr* low_out, char** name_out)
{
  Dwarf_Die child = NULL;
  if (dwarf_child(die, &child, &err) != DW_DLV_OK)
    return 0;

  int ret = 0;
  for (;;) {
    Dwarf_Half tag;
    if (dwarf_tag(child, &tag, &err) == DW_DLV_OK
        && tag == DW_TAG_subprogram) {

      if ((ret = jdebug_dwarf_check_subprog(child, target_addr, err, low_out, name_out)))
        goto done;
    }

    Dwarf_Die next = NULL;
    if (dwarf_siblingof_b(dbg, child, 1, &next, &err) != DW_DLV_OK)
      break;

    dwarf_dealloc_die(child);
    child = next;
  }

done:
  if (child)
    dwarf_dealloc_die(child);
  return ret;
}

static int jdebug_mapping_get_source_lines_die(Dwarf_Debug dbg, Dwarf_Die die, Dwarf_Error* err, size_t target_addr, char** source_file, uint64_t* source_line)
{
  int ret                 = -1;
  Dwarf_Unsigned version  = 0;
  Dwarf_Small table_count = 0;
  Dwarf_Line_Context ctx  = NULL;
  Dwarf_Line* lines       = NULL;
  Dwarf_Signed line_count = 0;
  Dwarf_Line best         = NULL;
  Dwarf_Addr best_addr    = 0;
  char* src               = NULL;
  Dwarf_Unsigned lineno   = 0;

  if (dwarf_srclines_b(die, &version, &table_count, &ctx, err) != DW_DLV_OK) {
    return 0;
  }

  if (dwarf_srclines_from_linecontext(ctx, &lines, &line_count, err) != DW_DLV_OK) {
    goto done;
  }

  for (Dwarf_Signed i = 0; i < line_count; i++) {
    Dwarf_Addr row_addr     = 0;
    Dwarf_Bool end_sequence = 0;

    if (dwarf_lineaddr(lines[i], &row_addr, err) != DW_DLV_OK)
      continue;

    dwarf_lineendsequence(lines[i], &end_sequence, err);

    if (end_sequence) {
      continue;
    }

    if (dwarf_linesrc(lines[i], &src, err) != DW_DLV_OK)
      goto done;

    if (dwarf_lineno(lines[i], &lineno, err) != DW_DLV_OK)
      goto done;

    if (row_addr <= target_addr && (!best || row_addr > best_addr)) {
      best      = lines[i];
      best_addr = row_addr;
    }
  }

  Dwarf_Addr row_addr;

  if (!best)
    goto done;

  if (dwarf_lineaddr(best, &row_addr, err) != DW_DLV_OK)
    goto done;

  if (dwarf_linesrc(best, &src, err) != DW_DLV_OK)
    goto done;

  if (dwarf_lineno(best, &lineno, err) != DW_DLV_OK)
    goto done;

  *source_file = strdup(src);
  if (!*source_file)
    goto done;

  *source_line = lineno;

  ret = 1;

done:
  if (src)
    dwarf_dealloc(dbg, src, DW_DLA_STRING);
  dwarf_srclines_dealloc_b(ctx);
  return ret;
}

static int jdebug_find_symbol_dwarf(IMAGEHLP_MODULE* module_info, size_t pc, symbol_info_t* result)
{
  int ret            = -1;
  Dwarf_Debug dbg    = NULL;
  Dwarf_Error err    = NULL;
  Dwarf_Half version = 0;

  const size_t target_addr = pc - module_info->BaseOfImage + 0x140000000 - 1;

  if (dwarf_init_path(module_info->ImageName, NULL, 0, DW_GROUPNUMBER_ANY, NULL, NULL, &dbg, &err) != DW_DLV_OK)
    goto done;

  while (dwarf_next_cu_header_d(dbg, 1, NULL, &version, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &err) == DW_DLV_OK) {
    Dwarf_Die die = NULL;

    if (dwarf_siblingof_b(dbg, NULL, 1, &die, &err) != DW_DLV_OK)
      continue;

    if (!jdebug_dwarf_die_in_range(die, target_addr, err)) {
      dwarf_dealloc_die(die);
      continue;
    }

    Dwarf_Addr prog_low = 0;
    char* prog_name     = NULL;
    ret                 = jdebug_dwarf_find_subprog(dbg, die, target_addr, err, &prog_low, &prog_name);
    if (!ret)
      break;

    result->start_address = prog_low - 0x140000000;
    result->displacement  = target_addr - prog_low;
    result->name          = prog_name;

    char* source_file    = NULL;
    uint64_t source_line = 0;
    if (jdebug_mapping_get_source_lines_die(dbg, die, &err, target_addr, &source_file, &source_line)) {
      result->filename = source_file;
      result->line     = source_line;
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
