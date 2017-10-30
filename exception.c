#include <setjmp.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "exception.h"

jmp_buf except_buf;

EXCEPT *except;

void throw(const char *desc, const char *what)
{
    except = malloc(strlen(what) + (sizeof *except));
    except->desc = desc;
    strcpy(except->what, what);
    longjmp(except_buf, true);
}
