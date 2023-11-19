/* radare @ pancake @ nopcode.org */

#if 0

PAS - parse assembly strings

Specifies a basic interface for parsing assembly strings
to prepare frontends on top of it for multiple architectures.

This way we reach a faster way to write a code analysis
engine without having to twice the job of parsing the
assembly which tends to bring errors.

#endif

#include "main.h"
#include "code.h"

void pas_set_arch(const char *str)
{
}

#if 0
BUGS:
 - needs to run 'pd' once before working :??? why?
 - current seek is not used properly or so
#endif

/* TODO: Use vm.c from here */
int pas_emul_init()
{
	vm_init(0);
	return 0;
}

int pas_emul(const char *str)
{
	const char *ptr;
	ptr = strchr(str, '=');
	if (ptr == str)
		return -1;
	if (ptr != NULL) {
		/* huh */
	}
	return 0;
}

int pas_aop_mips(int argc, const char *argv[], struct aop_t *aop, char *newstr)
{
	int i,j,k;
	struct {
		char *op;
		char *str;
		char *str12eq;
		int type;
	} ops[] = {
		{ "li",  "1 = 2", "...", AOP_TYPE_MOV },
		{ "lui",  "1 = 2", "...", AOP_TYPE_MOV },
		{ "move",  "1 = 2", "...", AOP_TYPE_CMP },
		{ "and",  "1 = 2 & 3", "and 1 &= 3",   AOP_TYPE_MOV },
		{ "addi",  "1 = 2 + 3", "1 += 3",   AOP_TYPE_MOV },
		{ "addu",  "1 = 2 + 3", "1 += 3",   AOP_TYPE_MOV },
		{ "addiu",  "1 = 2 + 3", "1 += 3",   AOP_TYPE_MOV },
		{ "subu",  "1 = 2 - 3",   "1 -= 3",  AOP_TYPE_MOV },
		{ "subi",  "1 = 2 - 3", "1 -= 3",    AOP_TYPE_MOV },
		{ "lbu",   "1 = [.8: 2 ]", "", AOP_TYPE_LOAD},
		{ "lw",    "1 = [.3.2: 2 ]", "",   AOP_TYPE_LOAD },
		{ "sw",    "[.3.2: 2 ] = 1", "",   AOP_TYPE_STORE },
		{ "bal",   "call 1", "", AOP_TYPE_CALL },
		{ "b",   "jmp 1", "", AOP_TYPE_JMP },
		{ "beqzl",   "jbe 1", "", AOP_TYPE_CJMP },
		{ "bnez",   "jnz 1", "", AOP_TYPE_CJMP },
		{ "beqz",   "jz 1", "", AOP_TYPE_CJMP },
		{ "bne",   "jne 1", "", AOP_TYPE_CJMP },
		{ "nop",   "...", "", AOP_TYPE_NOP },
		{ "jalr",  "call 1", "", AOP_TYPE_CALL },
		{ "jr ",  "jmp 1", "", AOP_TYPE_CALL },
		{ NULL }
	};

	if((!strcmp(argv[0], "jalr")) || (!strcmp(argv[0], "jr"))) {
		if (!strcmp(argv[1], "ra")) {
			strcpy(newstr, "ret");
			if (aop != NULL)
				aop->type = AOP_TYPE_RET;
			return 1;
		}
	}

	if (newstr != NULL) {
		for(i=0;ops[i].op != NULL;i++) {
			if (!strcmp(ops[i].op, argv[0])) {
				const char *str = ops[i].str;
				/* opcode matches */
				if (aop != NULL) {
					aop->type = ops[i].type;
				}
				if (argc>2 && !strcmp(argv[1], argv[2]))
						str = ops[i].str12eq;
				for(j=k=0;ops[i].str[j]!='\0';j++,k++) {
					if (str[j]=='.') {
						j++;
						newstr[k] = str[j];
					} else
					if (str[j]>='0' && str[j]<='9') {
						const char *w = argv[ str[j]-'0' ];
						if (w != NULL) {
							strcpy(newstr+k, w);
							k += strlen(w)-1;
						}
					} else newstr[k] = str[j];
				}
				newstr[k]='\0';
				return 1;
			}
		}
	}

	if (aop != NULL) {
		/* XXX : we need bytes */
	}

	if (newstr != NULL) {
		for (i=0; i<argc; i++) {
			strcat(newstr, argv[i]);
			strcat(newstr, (i == 0 || i== argc - 1)?" ":",");
		}
	}

	return 0;

}

/* beutify assembly to be compilable by gas :) */
int pas_aop_x86_gas(int argc, const char *argv[], struct aop_t *aop, char *newstr)
{
	int i;
	struct {
		char *op;
		char *str;
		int type;
	} ops[] = {
		{ "call", "call 1",   AOP_TYPE_CALL },
		{ "jmp",  "jmp 1",   AOP_TYPE_JMP },
		{ "je",   "je 1",     AOP_TYPE_CJMP },
		{ "jz",   "jz 1",     AOP_TYPE_CJMP },
		{ "jnz",  "jnz 1",     AOP_TYPE_CJMP },
		{ "jg",   "jg 1",     AOP_TYPE_CJMP },
		{ "jge",  "jge 1",     AOP_TYPE_CJMP },
// not implemented !!!
// 0x00001269   -8             e30f            jecxz 0x127a

		{ NULL }
	};

	for(i=0;ops[i].op != NULL;i++) {
		if (!strcmp(ops[i].op, argv[0])) {
			/* opcode matches */
			if (aop != NULL) {
				aop->type = ops[i].type;
			}
			if (newstr != NULL) {
				char *p = strchr(argv[1], ' ');
				char flagstr[256];
				ut64 val;
				if (p != NULL) p = p + 1;
				else p = (char *)argv[1];
				val = get_offset(p);
				flagstr[0]='\0';
				string_flag_offset(NULL, flagstr, val, 0);
				if (flagstr[0]=='\0' || strchr(flagstr,'+')) {
					sprintf(flagstr, "sub_0x%08llx", val);
					flag_set(flagstr, val, 0);
				}
				sprintf(newstr, "%s %s", argv[0], flagstr);
			}
			return 1;
		}
	}

	if (aop != NULL) {
		/* XXX : we need bytes */
	}

	if (newstr != NULL) {
		for (i=0; i<argc; i++) {
			strcat(newstr, argv[i]);
			strcat(newstr, (i == 0 || i== argc - 1)?" ":",");
		}
	}

	return 0;
}

int pas_aop_x86(int argc, const char *argv[], struct aop_t *aop, char *newstr)
{
	int i,j,k;
	struct {
		char *op;
		char *str;
		int type;
	} ops[] = {
		{ "cmp",  "cmp 1, 2", AOP_TYPE_CMP },
		{ "test", "cmp 1, 2", AOP_TYPE_CMP },
		{ "lea",  "1 = 2",    AOP_TYPE_MOV },
		{ "mov",  "1 = 2",    AOP_TYPE_MOV },
		{ "cmovl","ifnot zf,1 = 2",    AOP_TYPE_MOV },
		{ "xor",  "1 ^= 2",   AOP_TYPE_XOR },
		{ "and",  "1 &= 2",   AOP_TYPE_AND},
		{ "or",   "1 |= 2",   AOP_TYPE_OR },
		{ "add",  "1 += 2",   AOP_TYPE_ADD },
		{ "sub",  "1 -= 2",   AOP_TYPE_SUB },
		{ "mul",  "1 *= 2",   AOP_TYPE_MUL },
		{ "div",  "1 /= 2",   AOP_TYPE_DIV },
		{ "call", "call 1",   AOP_TYPE_CALL },
		{ "jmp",  "goto 1",   AOP_TYPE_JMP },
		{ "je",   "je 1",     AOP_TYPE_CJMP },
		{ "push", "push 1",   AOP_TYPE_PUSH },
		{ "pop",  "pop 1",    AOP_TYPE_POP },
		{ "ret",  "ret",      AOP_TYPE_RET},
		{ NULL }
	};

	for(i=0;ops[i].op != NULL;i++) {
		if (!strcmp(ops[i].op, argv[0])) {
			/* opcode matches */
			if (aop != NULL) {
				aop->type = ops[i].type;
			}
			if (newstr != NULL) {
				for(j=k=0;ops[i].str[j]!='\0';j++,k++) {
					if (ops[i].str[j]>='0' && ops[i].str[j]<='9') {
						const char *w = argv[ ops[i].str[j]-'0' ];
						if (w != NULL) {
							strcpy(newstr+k, w);
							k += strlen(w)-1;
						}
					} else newstr[k] = ops[i].str[j];
				}
				newstr[k]='\0';
			}
			return 1;
		}
	}

	if (aop != NULL) {
		/* XXX : we need bytes */
	}

	if (newstr != NULL) {
		for (i=0; i<argc; i++) {
			strcat(newstr, argv[i]);
			strcat(newstr, (i == 0 || i== argc - 1)?" ":",");
		}
	}

	return 0;
}

int pas_aop_gas(int argc, const char *argv[], struct aop_t *aop, char *newstr)
{
	switch(config.arch) {
	case ARCH_MIPS:
		//return pas_aop_mips(argc, argv, aop, newstr);
	case ARCH_X86:
		return pas_aop_x86_gas(argc, argv, aop, newstr);
	}
	return 0;
}

int pas_aop_aop(int argc, const char *argv[], struct aop_t *aop, char *newstr)
{
	switch(config.arch) {
	case ARCH_MIPS:
		return pas_aop_mips(argc, argv, aop, newstr);
	case ARCH_X86:
		return pas_aop_x86(argc, argv, aop, newstr);
	}
	return 0;
}

struct aop_t *pas_aop(int arch, ut64 seek, const u8 *bytes, int len, struct aop_t *aop, char *newstr, int pseudo)
{
	int i;
	char str[64];
	char w0[32];
	char w1[32];
	char w2[32];
	char w3[32];
	char w4[32];
	char *ptr, *optr;

	// XXX asm.syntax should be 'intel'
	udis_arch_string(arch, str, bytes, config.endian, seek, 0, 0);

	if (str[0]!='\0') {
		w0[0]='\0';
		w1[0]='\0';
		w2[0]='\0';
		w3[0]='\0';
		ptr = strchr(str, ' ');
		if (ptr == NULL)
			ptr = strchr(str, '\t');
		if (ptr) {
			ptr[0]='\0';
			for(ptr=ptr+1;ptr[0]==' ';ptr=ptr+1);
			strcpy(w0, str);
			strcpy(w1, ptr);

			optr=ptr;
			ptr = strchr(ptr, ',');
			if (ptr) {
				ptr[0]='\0';
				for(ptr=ptr+1;ptr[0]==' ';ptr=ptr+1);
				strcpy(w1, optr);
				strcpy(w2, ptr);
				ptr = strchr(ptr, ',');
				if (ptr) {
					ptr[0]='\0';
					for(ptr=ptr+1;ptr[0]==' ';ptr=ptr+1);
					strcpy(w2, optr);
					strcpy(w3, ptr);
				}
			}
		}
		{
			int nw = 0;
			const char *wa[] = {
				(const char*) &w0,
				(const char *) &w1,
				(const char *) &w2,
				(const char *) &w3,
				(const char *) &w4
			};

			for(i=0;i<4;i++) {
				if (wa[i][0] != '\0')
				nw++;
			}
			if (pseudo)
				pas_aop_aop(nw, wa, aop, newstr);
			else pas_aop_gas(nw, wa, aop, newstr);
		}
	}
	return NULL;
}
