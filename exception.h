enum exception_type {
    NO_EXCEPTION = 0,
	EXCEPTION,
    SIMPLE_EXCEPTION,
};
enum simple_exception_type {
	GET_A_FAILURE,
	GET_B_FAILURE,
    OPCODE_FAILURE,
};
struct exception {
	int tag;
};
struct simple_exception {
	struct exception base;
    int type;
	char what[];
};
jmp_buf exception;
struct exception* exception_value;
#define THROW(exc_type, exc_what) do {\
    char *_what = (exc_what);\
    struct simple_exception *_exc = malloc(sizeof(struct simple_exception) + strlen(_what));\
    _exc->base.tag = SIMPLE_EXCEPTION;\
    _exc->type = (exc_type);\
    strcpy(_exc->what, _what);\
    exception_value = (struct exception *)_exc;\
    longjmp(exception, SIMPLE_EXCEPTION); } while (0)
