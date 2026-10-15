#pragma once

#include <stddef.h>

#define JCONTAINER_NEEDS_STLIB 0
#define JCONTAINER_NEEDS_STRING 0

#ifndef jcontainer_malloc
#undef JCONTAINER_NEEDS_STLIB
#define JCONTAINER_NEEDS_STLIB 1
#define jcontainer_malloc malloc
#endif

#ifndef jcontainer_free
#undef JCONTAINER_NEEDS_STLIB
#define JCONTAINER_NEEDS_STLIB 1
#define jcontainer_free free
#endif

#ifndef jcontainer_realloc
#undef JCONTAINER_NEEDS_STLIB
#define JCONTAINER_NEEDS_STLIB 1
#define jcontainer_realloc realloc
#endif

#ifndef jcontainer_memcpy
#undef JCONTAINER_NEEDS_STRING
#define JCONTAINER_NEEDS_STRING 1
#define jcontainer_memcpy memcpy
#endif

#ifndef jcontainer_memmove
#undef JCONTAINER_NEEDS_STRING
#define JCONTAINER_NEEDS_STRING 1
#define jcontainer_memmove memmove
#endif

#define JCONTAINER_NULL ((void*) 0)

#if JCONTAINER_NEEDS_STLIB == 1
#include <stdlib.h>
#endif

#if JCONTAINER_NEEDS_STRING == 1
#include <string.h>
#endif

#ifdef _MSC_VER
#define JCONTAINER_NODISCARD _Check_return_
#elif defined(__GNUC__) || defined(__clang__)
#define JCONTAINER_NODISCARD __attribute__((__warn_unused_result__))
#endif

typedef struct
{
  void* base;
  size_t size;
  size_t capacity;
} jcontainer_array_generic_t;

#define JCONTAINER_DECLARE_ARRAY(element_type_arg_, type_name_arg_) \
  typedef struct                                                    \
  {                                                                 \
    __typeof__(element_type_arg_)* base;                            \
    size_t size;                                                    \
    size_t capacity;                                                \
  } type_name_arg_;

#define jcontainer_array_init {JCONTAINER_NULL, 0, 0}

#define jcontainer_array_safe_generic2_(array_ptr_arg_)          \
  ((void) sizeof(struct {                                        \
     _Static_assert(                                             \
       offsetof(__typeof__(*(array_ptr_arg_)), base)             \
           == offsetof(jcontainer_array_generic_t, base)         \
         && offsetof(__typeof__(*(array_ptr_arg_)), size)        \
              == offsetof(jcontainer_array_generic_t, size)      \
         && offsetof(__typeof__(*(array_ptr_arg_)), capacity)    \
              == offsetof(jcontainer_array_generic_t, capacity), \
       "invalid array");                                         \
     int dummy__;                                                \
   }),                                                           \
   (jcontainer_array_generic_t*) (array_ptr_arg_))

#ifdef _MSC_VER
#define jcontainer_array_safe_generic1_(...) \
  __pragma(warning(push)) __pragma(warning(disable : 4116)) jcontainer_array_safe_generic2_(__VA_ARGS__) __pragma(warning(pop))
#else
#define jcontainer_array_safe_generic1_(...) jcontainer_array_safe_generic2_(__VA_ARGS__)
#endif

#define jcontainer_array_safe_generic_(array_ptr_arg_) \
  (_Generic((array_ptr_arg_)->size,                    \
     size_t: _Generic((array_ptr_arg_)->capacity,      \
       size_t: jcontainer_array_safe_generic1_(array_ptr_arg_))))

#define jcontainer_array_safe_element_(array_ptr_arg_, element_ptr_arg_) \
  (_Generic((element_ptr_arg_),                                          \
     __typeof__((array_ptr_arg_)->base): (element_ptr_arg_)))

#define jcontainer_array_element_size(array_ptr_arg_) \
  (sizeof(__typeof__(*((array_ptr_arg_)->base))))

inline static JCONTAINER_NODISCARD int
jcontainer_array_ensure_capacity_generic(
  jcontainer_array_generic_t* array,
  const size_t new_size,
  const size_t element_size)
{
  size_t new_capacity = array->capacity;

  if (0 == new_capacity)
    new_capacity = 1;

  while (new_capacity < new_size)
    new_capacity *= 2;

  if (new_capacity != array->capacity) {
    void* new_base = jcontainer_realloc(array->base, new_capacity * element_size);

    if (NULL == new_base)
      return -1;

    array->base     = new_base;
    array->capacity = new_capacity;
  }

  return 0;
}

#define jcontainer_array_ensure_capacity(array_ptr_arg_, new_size_arg_, element_ptr_arg_) \
  jcontainer_array_ensure_capacity_generic(                                               \
    jcontainer_array_safe_generic_(array_ptr_arg_),                                       \
    (new_size_arg_),                                                                      \
    jcontainer_array_element_size(array_ptr_arg_))

inline static JCONTAINER_NODISCARD int
jcontainer_array_push_back_generic(
  jcontainer_array_generic_t* array,
  const void* element_ptr,
  const size_t element_size)
{
  if (jcontainer_array_ensure_capacity_generic(array, array->size + 1, element_size))
    return -1;

  jcontainer_memcpy((char*) array->base + element_size * array->size,
                    element_ptr, element_size);
  ++array->size;
  return 0;
}

#define jcontainer_array_push_back(array_ptr_arg_, element_ptr_arg_)  \
  jcontainer_array_push_back_generic(                                 \
    jcontainer_array_safe_generic_(array_ptr_arg_),                   \
    jcontainer_array_safe_element_(array_ptr_arg_, element_ptr_arg_), \
    jcontainer_array_element_size(array_ptr_arg_))

inline static JCONTAINER_NODISCARD int
jcontainer_array_insert_generic(
  jcontainer_array_generic_t* array,
  const size_t pos,
  const void* element_ptr,
  const size_t element_size)
{
  if (pos > array->size)
    return -1;

  if (pos == array->size)
    return jcontainer_array_push_back_generic(array, element_ptr, element_size);

  if (jcontainer_array_ensure_capacity_generic(array, array->size + 1, element_size))
    return -1;

  jcontainer_memmove((char*) array->base + element_size * (pos + 1),
                     (char*) array->base + element_size * pos,
                     element_size * (array->size - pos));
  jcontainer_memcpy((char*) array->base + element_size * pos,
                    element_ptr,
                    element_size);
  ++array->size;
  return 0;
}

#define jcontainer_array_insert(array_ptr_arg_, pos_arg_, element_ptr_arg_) \
  jcontainer_array_insert_generic(                                          \
    jcontainer_array_safe_generic_(array_ptr_arg_),                         \
    (pos_arg_),                                                             \
    jcontainer_array_safe_element_(array_ptr_arg_, element_ptr_arg_),       \
    jcontainer_array_element_size(array_ptr_arg_))

inline static JCONTAINER_NODISCARD int
jcontainer_array_remove_generic(
  jcontainer_array_generic_t* array,
  const size_t pos,
  const size_t element_size)
{
  if (pos >= array->size)
    return -1;

  if (pos == array->size - 1) {
    --array->size;
    return 0;
  }

  jcontainer_memmove((char*) array->base + element_size * pos,
                     (char*) array->base + element_size * (pos + 1),
                     element_size * (array->size - pos - 1));
  --array->size;

  return 0;
}

#define jcontainer_array_remove(array_ptr_arg_, pos_arg_) \
  jcontainer_array_remove_generic(                        \
    jcontainer_array_safe_generic_(array_ptr_arg_),       \
    (pos_arg_),                                           \
    jcontainer_array_element_size(array_ptr_arg_))

#define jcontainer_array_fini(array_ptr_arg_)                \
  do {                                                       \
    __typeof__((array_ptr_arg_)) array__ = (array_ptr_arg_); \
    jcontainer_free(array__->base);                          \
    array__->base     = JCONTAINER_NULL;                     \
    array__->size     = 0;                                   \
    array__->capacity = 0;                                   \
  } while (0)


#define JCONTAINER_LL_DEFAULT_NEXT_FIELD next_
#define JCONTAINER_LL_FIELD_TYPED(type_arg_) type_arg_* JCONTAINER_LL_DEFAULT_NEXT_FIELD;
#define JCONTAINER_LL_FIELD JCONTAINER_LL_FIELD_TYPED(void)

#define jcontainer_ll_next_ptr_from_offset(current_arg_, next_offset_arg_) \
  (_Generic((current_arg_), void*: (void*) ((char*) (current_arg_) + (next_offset_arg_))))

#define jcontainer_ll_set_next_from_offset(current_arg_, next_offset_arg_, value_arg_) \
  (*(void**) (jcontainer_ll_next_ptr_from_offset(current_arg_, next_offset_arg_)) = (value_arg_))

inline static JCONTAINER_NODISCARD int
jcontainer_ll_push_back_generic_(
  void** current,
  const void* element,
  const size_t element_size,
  const size_t next_offset)
{
  void* new_element;

  while (JCONTAINER_NULL != *current)
    current = jcontainer_ll_next_ptr_from_offset(*current, next_offset);

  new_element = jcontainer_malloc(element_size);
  if (JCONTAINER_NULL == new_element)
    return -1;

  *current = new_element;
  jcontainer_memcpy(*current, element, element_size);
  jcontainer_ll_set_next_from_offset(*current, next_offset, JCONTAINER_NULL);
  return 0;
}

#define jcontainer_ll_push_back_generic(ptr_arg_, element_ptr_arg_, next_field_arg_) \
  jcontainer_ll_push_back_generic_(                                                  \
    (void**) (ptr_arg_),                                                             \
    (element_ptr_arg_),                                                              \
    sizeof(__typeof__(*(element_ptr_arg_))),                                         \
    offsetof(__typeof__(**(ptr_arg_)), next_field_arg_))

#define jcontainer_ll_size_generic(head_ptr_arg_, size_ptr_arg_, next_field_arg_) \
  do {                                                                            \
    __typeof__(*(head_ptr_arg_)) current__ = *(head_ptr_arg_);                    \
    __typeof__((size_ptr_arg_)) size__     = (size_ptr_arg_);                     \
    *size__                                = 0;                                   \
    while (JCONTAINER_NULL != current__) {                                        \
      ++*size__;                                                                  \
      current__ = current__->next_field_arg_;                                     \
    }                                                                             \
  } while (0)

#define jcontainer_ll_foreach_generic(head_ptr_arg_, iterator_arg_, next_field_arg_) \
  for (__typeof__(*(head_ptr_arg_)) iterator_arg_ = *(head_ptr_arg_);                \
       JCONTAINER_NULL != iterator_arg_;                                             \
       iterator_arg_ = iterator_arg_->next_field_arg_)

#define jcontainer_ll_clear_generic(head_ptr_arg_, next_field_arg_) \
  do {                                                              \
    __typeof__((head_ptr_arg_)) head_ptr__ = (head_ptr_arg_);       \
    __typeof__(*head_ptr__) head__         = *head_ptr__;           \
    while (JCONTAINER_NULL != head__) {                             \
      __typeof__(head__) current__ = head__;                        \
      head__                       = head__->next_field_arg_;       \
      jcontainer_free(current__);                                   \
    }                                                               \
    *head_ptr__ = JCONTAINER_NULL;                                  \
  } while (0)

inline static JCONTAINER_NODISCARD int
jcontainer_ll_insert_generic_(
  void** current,
  const size_t pos,
  const void* element,
  const size_t element_size,
  const size_t next_offset)
{
  void* prev;
  void* new_element;

  for (size_t i = 0;
       JCONTAINER_NULL != *current && i < pos;
       current = jcontainer_ll_next_ptr_from_offset(*current, next_offset), ++i) {}

  prev        = *current;
  new_element = jcontainer_malloc(element_size);

  if (JCONTAINER_NULL == new_element)
    return -1;

  *current = new_element;
  jcontainer_memcpy(*current, element, element_size);
  jcontainer_ll_set_next_from_offset(*current, next_offset, prev);
  return 0;
}

#define jcontainer_ll_insert_generic(head_ptr_arg_, pos_arg_, element_ptr_arg_, next_field_arg_) \
  jcontainer_ll_insert_generic_((void**) (head_ptr_arg_),                                        \
                                (pos_arg_),                                                      \
                                (element_ptr_arg_),                                              \
                                sizeof(__typeof__(*(element_ptr_arg_))),                         \
                                offsetof(__typeof__(**(head_ptr_arg_)), next_field_arg_))

#define jcontainer_ll_remove_generic(head_ptr_arg_, pos_arg_, next_field_arg_) \
  do {                                                                         \
    __typeof__((head_ptr_arg_)) head_ptr__ = (head_ptr_arg_);                  \
    __typeof__(head_ptr__) current__       = head_ptr__;                       \
    __typeof__(*current__) prev__          = JCONTAINER_NULL;                  \
    size_t i__                             = 0;                                \
    size_t pos__                           = (pos_arg_);                       \
    for (; JCONTAINER_NULL != *current__ && i__ < pos__; ++i__) {              \
      prev__    = *current__;                                                  \
      current__ = (__typeof__(current__)) &(*current__)->next_field_arg_;      \
    }                                                                          \
    if (JCONTAINER_NULL != *current__ && i__ == pos__) {                       \
      __typeof__(*current__) remove__ = *current__;                            \
      if (JCONTAINER_NULL != prev__)                                           \
        prev__->next_field_arg_ = remove__->next_field_arg_;                   \
      else                                                                     \
        *head_ptr__ = remove__->next_field_arg_;                               \
      jcontainer_free(remove__);                                               \
    }                                                                          \
  } while (0)

inline static JCONTAINER_NODISCARD void*
jcontainer_ll_at_generic_(
  void** head,
  const size_t pos,
  const size_t next_offset)
{
  void* current = *head;
  for (size_t i = 0;
       i < pos && JCONTAINER_NULL != current;
       ++i) {
    current = *(void**) jcontainer_ll_next_ptr_from_offset(current, next_offset);
  }
  return current;
}

#define jcontainer_ll_at_generic(head_ptr_arg_, pos_arg_, next_field_arg_) \
  jcontainer_ll_at_generic_((void**) (head_ptr_arg_),                      \
                            (pos_arg_),                                    \
                            offsetof(__typeof__(**(head_ptr_arg_)), next_field_arg_))

inline static JCONTAINER_NODISCARD void*
jcontainer_ll_back_generic_(
  void** current_ptr,
  const size_t next_offset)
{
  void* current = *current_ptr;
  void* prev    = JCONTAINER_NULL;
  while (JCONTAINER_NULL != current) {
    prev    = current;
    current = *(void**) jcontainer_ll_next_ptr_from_offset(current, next_offset);
  }
  return prev;
}

#define jcontainer_ll_back_generic(ptr_arg_, next_field_arg_) \
  jcontainer_ll_back_generic_((void**) (ptr_arg_),            \
                              offsetof(__typeof__(**(ptr_arg_)), next_field_arg_))


#define jcontainer_ll_push_back(ptr_arg_, element_ptr_arg_) \
  jcontainer_ll_push_back_generic(ptr_arg_, element_ptr_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_size(head_ptr_arg_, size_ptr_arg_) \
  jcontainer_ll_size_generic(head_ptr_arg_, size_ptr_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_foreach(head_ptr_arg_, iterator_arg_) \
  jcontainer_ll_foreach_generic(head_ptr_arg_, iterator_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_clear(head_ptr_arg_) \
  jcontainer_ll_clear_generic(head_ptr_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_insert(head_ptr_arg_, pos_arg_, element_ptr_arg_) \
  jcontainer_ll_insert_generic(head_ptr_arg_, pos_arg_, element_ptr_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_remove(head_ptr_arg_, pos_arg_) \
  jcontainer_ll_remove_generic(head_ptr_arg_, pos_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_at(head_ptr_arg_, pos_arg_) \
  jcontainer_ll_at_generic(head_ptr_arg_, pos_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)

#define jcontainer_ll_back(head_ptr_arg_) \
  jcontainer_ll_back_generic(head_ptr_arg_, JCONTAINER_LL_DEFAULT_NEXT_FIELD)
