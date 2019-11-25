typedef struct {
	const char *desc;
	const char *what;
} EXCEPT;
extern jmp_buf except_buf;
extern EXCEPT* except;
extern void throw(const char *desc, const char *what);
