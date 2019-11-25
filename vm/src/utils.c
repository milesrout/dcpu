#include <stdio.h>
#include <stdlib.h>

/* can't include utils.h because it poisons identifiers we want to use here */

static const char *cerr = "%s:%d: Could not allocate %lu-array of %lu bytes\n";
static const char *merr = "%s:%d: Could not allocate %lu bytes\n";
static const char *rerr = "%s:%d: Could not reallocate %p to %lu bytes\n";

void *ecalloc(const char *file, int line, size_t k, size_t n)
{
	void *q = calloc((k), (n));
	if (q == NULL) {
		fprintf(stderr, cerr, file, line, k, n);
		abort();
	}

	return q;
}

void *emalloc(const char *file, int line, size_t n)
{
	void *p = malloc(n);

	if (p == NULL) {
		fprintf(stderr, merr, file, line, n);
		abort();
	}

	return p;
}

void *erealloc(const char *file, int line, void *p, size_t n)
{
	void *q = realloc(p, n);

	if (q == NULL) {
		fprintf(stderr, rerr, file, line, p, n);
		abort();
	}

	return q;
}
