#include <assert.h>
#include <stdio.h>

int main()
{
	int err;

	err = fprintf(stdout, "Hello, world!\n");
	assert(err >= 0);

	return 0;
}
