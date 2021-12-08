#include <owl/internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

void *owl_dbg_malloc_(size_t s, char const *f, int l) {
  void *p = malloc(s);
  printf("%p = OWL_MALLOC(%lu) in file %s at line %d\n", p, s, f, l);
  return p;
}

void *owl_dbg_realloc_(void *p, size_t s, char const *f, int l) {
  void *np = realloc(p, s);
  printf("%p = OWL_REALLOC(%p, %lu) in file %s at line %d\n", np, p, s, f, l);
  return realloc(p, s);
}

void owl_dbg_free_(void *p, char const *f, int l) {
  printf("OWL_FREE(%p) in file %s at line %d\n", p, f, l);
  free(p);
}

void owl_dbg_log_(char const *f, int l, char const *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  printf("OWL_DBG_LOG in file %s at line %d: ", f, l);
  vprintf(fmt, args);
  va_end(args);
}
