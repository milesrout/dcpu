#pragma GCC poison calloc
void *ecalloc(const char *file, int line, size_t k, size_t n);
#define ecalloc(k, n) ecalloc(__FILE__, __LINE__, (k), (n))
#pragma GCC poison malloc
void *emalloc(const char *file, int line, size_t n);
#define emalloc(n) emalloc(__FILE__, __LINE__, (n))
#pragma GCC poison realloc
void *erealloc(const char *file, int line, void *p, size_t n);
#define erealloc(p, n) erealloc(__FILE__, __LINE__, (p), (n))
#define get_member_of(t1, value, member) (((t1*)((value)->data))->member)
