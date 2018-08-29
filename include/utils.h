#define ecalloc(k, n) ({ void *q = calloc((k), (n)); if (q == NULL) { fprintf(stderr, "%s:%d: Could not zero-allocate %lu-array of %lu bytes\n", __FILE__, __LINE__, (size_t)(k), (size_t)(n)); abort(); } q; })
#pragma GCC poison calloc
#define emalloc(n)     ({ void *p = malloc(n); if (p == NULL) { fprintf(stderr, "%s:%d: Could not allocate %lu bytes\n", __FILE__, __LINE__, (size_t)(n)); abort(); } p; })
#pragma GCC poison malloc
#define erealloc(p, n) ({ void *q = realloc((p), (n)); if (q == NULL) { fprintf(stderr, "%s:%d: Could not reallocate %p to %lu bytes\n", __FILE__, __LINE__, (p), (size_t)(n)); abort(); } q; })
#pragma GCC poison realloc
#define get_member_of(type, value, member) (((type*)((value)->data))->member)
