#pragma once

#include <windows.h>

#include <DbgHelp.h>
#include <stdint.h>

typedef struct
{
  size_t start_address;
  size_t displacement;
  char* name;
  char* filename;
  uint64_t line;
} symbol_info_t;

int jdebug_find_symbol(IMAGEHLP_MODULE* module_info, size_t pc, symbol_info_t* result);
