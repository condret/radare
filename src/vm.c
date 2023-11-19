/*
 * Copyright (C) 2008, 2009
 *       pancake <youterm.com>
 *
 * radare is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * radare is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with radare; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include "radare.h"
#include "code.h"
#include "list.h"
#include "vm.h"

static struct list_head vm_regs;
static struct vm_cpu_t vm_cpu;
static ut64 vm_stack_base = 0;
static u8 *vm_stack = NULL;
static struct list_head vm_ops;
static struct list_head vm_mmu_cache;
extern void udis_set_pc(ut64 pc);

static int realio = 1;

struct vm_reg_type vm_reg_types[] = {
	{ VMREG_BIT, "bit" },
	{ VMREG_INT64, "int64" },
	{ VMREG_INT32, "int32" },
	{ VMREG_INT16, "int16" },
	{ VMREG_INT8, "int8" },
	{ VMREG_FLOAT32, "float32" },
	{ VMREG_FLOAT64, "float64" },
	{ 0, NULL }
};

int vm_mmu_real(int set)
{
	return realio = set;
}

void vm_reg_type_list()
{
	struct vm_reg_type *p = vm_reg_types;
	while(p) {
		if (p->str==NULL)
			break;
		cons_printf(" .%s\n", p->str);
		p++;
	}
}

static char *unkreg="(unk)";
const char *vm_reg_type(int type)
{
	struct vm_reg_type *p = vm_reg_types;
	while(p) {
		if (p->type == type)
			return p->str;
		p++;
	}
	return unkreg;
}

const int vm_reg_type_i(const char *str)
{
	struct vm_reg_type *p = vm_reg_types;
	while(p) {
		if (!strcmp(str, p->str))
			return p->type;
		p++;
	}
	return -1;
}

static ut64 vm_get_value(const char *str)
{
	ut64 ret = 0LL;
	for(;*str&&*str==' ';str=str+1);

	if (str[0]=='$' && str[1]=='$') {
		struct aop_t aop;
		char w[32];
		if (str[2]=='$') { // $$$
			ret = vm_reg_get(vm_cpu.pc);
			arch_aop(ret , config.block,&aop);
			return aop.length;
		} else { // $$
			return config.seek;
		}
	}

	if (str[0]=='0' && str[1]=='x')
		sscanf(str, "0x%llx", &ret);
	else
	if (str[0]>='0' && str[0]<='9')
		sscanf(str, "%lld", &ret);
	else ret = vm_reg_get(str);
	return ret;
}

static ut64 vm_get_math(const char *str)
{
	int len;
	char *p,*a;

	len = strlen(str)+1;
	p = alloca(len);
	memcpy(p, str, len);
	a = strchr(p,'+');
	if (a) {
		*a='\0';
		return vm_get_value(p) + vm_get_value(a+1);
	}
	a = strchr(p,'-');
	if (a) {
		*a='\0';
		return vm_get_value(p) - vm_get_value(a+1);
	}
	a = strchr(p,'*');
	if (a) {
		*a='\0';
		return vm_get_value(p) * vm_get_value(a+1);
	}
	a = strchr(p,'/');
	if (a) {
		*a='\0';
		return vm_get_value(p) / vm_get_value(a+1);
	}
	a = strchr(p,'&');
	if (a) {
		*a='\0';
		return vm_get_value(p) & vm_get_value(a+1);
	}
	a = strchr(p,'|');
	if (a) {
		*a='\0';
		return vm_get_value(p) | vm_get_value(a+1);
	}
	a = strchr(p,'^');
	if (a) {
		*a='\0';
		return vm_get_value(p) ^ vm_get_value(a+1);
	}
	a = strchr(p,'%');
	if (a) {
		*a='\0';
		return vm_get_value(p) % vm_get_value(a+1);
	}
	a = strchr(p,'>');
	if (a) {
		*a='\0';
		return vm_get_value(p) >> vm_get_value(a+1);
	}
	a = strchr(p,'<');
	if (a) {
		*a='\0';
		return vm_get_value(p) << vm_get_value(a+1);
	}
	return vm_get_value(p);
}

void vm_print(int type)
{
	int i;
	struct list_head *pos;

	if (type == -2)
		cons_printf("fs vm\n");

	list_for_each(pos, &vm_regs) {
		struct vm_reg_t *r = list_entry(pos, struct vm_reg_t, list);
		if (type == -2) {
			cons_printf("f vm.%s @ 0x%08llx\n", r->name, r->value);
		} else {
			if (type == -1 || type == r->type)
			cons_printf(".%s\t%s = 0x%08llx\n",
				vm_reg_type(r->type), r->name,
				(r->get!=NULL)?vm_reg_get(r->name):r->value);
		}
	}

	if (type == -2)
		cons_printf("fs *\n");
}
int vm_mmu_cache_write(ut64 addr, const u8 *buf, int len)
{
	struct vm_change_t *ch = (struct vm_change_t *)malloc(sizeof(struct vm_change_t));
	ch->from = addr;
	ch->to = addr + len;
	ch->data = (u8*)malloc(len);
	memcpy(ch->data, buf, len);
	list_add_tail(&(ch->list), &vm_mmu_cache);
	return 0;
}

int vm_mmu_cache_read(ut64 addr, u8 *buf, int len)
{
	struct vm_change_t *c;
	struct list_head *pos;

	// TODO: support for unaligned and partial accesses
	list_for_each(pos, &vm_mmu_cache) {
		c = list_entry(pos, struct vm_change_t, list);
		if (addr >= c->from && addr+len <= c->to) {
			memcpy(buf, c->data, len);
			return 1;
		}
	}
	return 0;
}

int vm_mmu_read(ut64 off, u8 *data, int len)
{
	if (!realio && vm_mmu_cache_read(off, data, len))
		return len;
	return radare_read_at(off, data, len);
}

int vm_mmu_write(ut64 off, const u8 *data, int len)
{
	if (!realio)
		return vm_mmu_cache_write(off, data, len);
	printf("WIte¡\n");
	return radare_write_at(off, data, len);
}

int vm_reg_add(const char *name, int type, ut64 value)
{
	struct vm_reg_t *r;

	r = (struct vm_reg_t *)malloc(sizeof (struct vm_reg_t));
	if (r == NULL)
		return 0;
	strncpy(r->name, name, 15);
	r->type = type;
	r->value = value;
	r->get = NULL;
	r->set = NULL;
	list_add_tail(&(r->list), &vm_regs);
	return 1;
}

struct vm_reg_t *rec = NULL;

ut64 vm_reg_get(const char *name)
{
	struct list_head *pos;
	int len = strlen(name);
	if (name[len-1]==']')
		len--;

	list_for_each(pos, &vm_regs) {
		struct vm_reg_t *r = list_entry(pos, struct vm_reg_t, list);
		if (!strncmp(name, r->name, len)) {
			if (rec==NULL && r->get != NULL) {
				ut64 val;
				rec = r;
				vm_eval(r->get);
				//vm_op_eval(r->get);
				rec = NULL;
				return r->value;
			}
			return r->value;
		}
	}
	return -1LL;
}

int vm_reg_del(const char *name)
{
	struct list_head *pos;

	list_for_each(pos, &vm_regs) {
		struct vm_reg_t *r = list_entry(pos, struct vm_reg_t, list);
		if (!strcmp(name, r->name)) {
			list_del(&r->list);
			return 0;
		}
	}
	return 1;
}

int vm_reg_set(const char *name, ut64 value)
{
	struct list_head *pos;

	list_for_each(pos, &vm_regs) {
		struct vm_reg_t *r = list_entry(pos, struct vm_reg_t, list);
		if (!strcmp(name, r->name)) {
			r->value = value;
			if (rec == NULL && r->set != NULL) {
				rec = r;
				vm_eval(r->set);
				rec = NULL;
			}
			return 1;
		}
	}
	return 0;
}

int vm_import(int vm)
{
	struct list_head *pos;

	printf("MMU: %s\n" , realio?"real":"cached");
	eprintf("Importing register values\n");
	list_for_each(pos, &vm_regs) {
		struct vm_reg_t *r = list_entry(pos, struct vm_reg_t, list);
		if (vm) {
			char name[64];
			snprintf(name, 63, "vm.%s", r->name);
			r->value = get_offset(name); // XXX doesnt work for eflags and so
		} else r->value = get_offset(r->name); // XXX doesnt work for eflags and so
	}
	return 0;
}


void vm_configure_flags(const char *zf)
{
	vm_cpu.zf = strdup(zf);
}

void vm_configure_cpu(const char *eip, const char *esp, const char *ebp)
{
	vm_cpu.pc = strdup(eip);
	vm_cpu.sp = strdup(esp);
	vm_cpu.bp = strdup(ebp);
}

void vm_configure_fastcall(const char *eax, const char *ebx, const char *ecx, const char *edx)
{
	vm_cpu.a0 = strdup(eax);
	vm_cpu.a1 = strdup(ebx);
	vm_cpu.a2 = strdup(ecx);
	vm_cpu.a3 = strdup(edx);
}

void vm_configure_ret(const char *eax)
{
	vm_cpu.ret = strdup(eax);
}

void vm_cpu_call(ut64 addr)
{
	/* x86 style */
	vm_stack_push(vm_reg_get(vm_cpu.pc));
	vm_reg_set(vm_cpu.pc, addr);
	// XXX this should be the next instruction after pc (we need insn length here)
}

void vm_stack_push(ut64 _val)
{
	// XXX determine size of stack here
	// XXX do not write while emulating zomfg
	ut32 val = _val;
	vm_reg_set(vm_cpu.sp, vm_reg_get(vm_cpu.sp)+4);
	vm_mmu_write(vm_reg_get(vm_cpu.sp), (u8*)&val, 4);
}

void vm_stack_pop(const char *reg)
{
	ut32 val = 0;
	if (vm_mmu_read(vm_reg_get(vm_cpu.sp), (ut8*)&val, 4))
		return;
//printf("POP (%s)\n", reg);
	vm_mmu_read(vm_reg_get(vm_cpu.sp), (ut8*)&val, 4);
	vm_reg_set(reg, val);
	vm_reg_set(vm_cpu.sp, vm_reg_get(vm_cpu.sp)-4);
}

int vm_op_add(const char *op, const char *str)
{
	struct vm_op_t *o;
	o = (struct vm_op_t *)malloc(sizeof(struct vm_op_t));
	strncpy(o->opcode, op, sizeof(o->opcode));
	strncpy(o->code, str, sizeof(o->code));
	list_add_tail(&(o->list), &vm_ops);
	return 0;
}

int vm_arch = -1;
int vm_init(int init)
{
	if (config.arch != vm_arch)
		init = 1;

	if (init) {
		INIT_LIST_HEAD(&vm_mmu_cache);
		INIT_LIST_HEAD(&vm_regs);
		INIT_LIST_HEAD(&vm_ops);
		memset(&vm_cpu, '\0', sizeof(struct vm_cpu_t));
	}

	vm_mmu_real(config_get_i("vm.realio"));
	/* vm_dbg_arch_x86_nregs */
	switch (config.arch) {
#if 0
	case ARCH_X86_64:
		vm_reg_add("rax", VMREG_INT64, 0);
		vm_reg_add("rbx", VMREG_INT64, 0);
		vm_reg_add("rcx", VMREG_INT64, 0);
		vm_reg_add("rdx", VMREG_INT64, 0);
		vm_reg_add("rdi", VMREG_INT64, 0);
		vm_reg_add("rsi", VMREG_INT64, 0);
		vm_reg_add("rip", VMREG_INT64, 0);
#endif
	case ARCH_X86:
		//eprintf("VM: Initialized\n");
		vm_op_add("mov", "$1=$2");
		vm_op_add("lea", "$1=$2");
		vm_op_add("add", "$1=$1+$2");
		vm_op_add("sub", "$1=$1-$2");
		vm_op_add("jmp", "eip=$1");
		vm_op_add("push", "esp=esp-4,[esp]=$1");
		vm_op_add("pop", "$1=[esp],esp=esp+4");
		vm_op_add("call", "esp=esp-4,[esp]=eip+$$$,eip=$1");
		vm_op_add("ret", "eip=[esp],esp=esp+4");

		vm_reg_add("eax", VMREG_INT32, 0);
		vm_reg_add("ax", VMREG_INT16, 0);
		vm_reg_alias("ax","ax=eax&0xffff", "eax=eax>16,eax=eax<16,eax=eax|ax");
		vm_reg_add("al", VMREG_INT8, 0);
		vm_reg_alias("al","al=eax&0xff", "al=al&0xff,eax=eax>16,eax=eax<16,eax=eax|al");
		vm_reg_add("ah", VMREG_INT8, 0);
		vm_reg_alias("ah","ah=eax&0xff00,ah=ah>8", "eax=eax&0xFFFF00ff,ah=ah<8,eax=eax|ah,ah=ah>8");
		vm_reg_add("ebx", VMREG_INT32, 0);
		vm_reg_add("ecx", VMREG_INT32, 0);
		vm_reg_add("edx", VMREG_INT32, 0);
		vm_reg_add("esi", VMREG_INT32, 0);
		vm_reg_add("edi", VMREG_INT32, 0);
		vm_reg_add("eip", VMREG_INT32, 0);
		vm_reg_add("esp", VMREG_INT32, 0);
		vm_reg_add("ebp", VMREG_INT32, 0);
		vm_reg_add("zf",  VMREG_BIT, 0);
		vm_reg_add("cf",  VMREG_BIT, 0); // ...

		vm_configure_cpu("eip", "esp", "ebp");
		vm_configure_flags("zf");
		//vm_configure_call("[ebp-4]", "[ebp-8]", "[ebp-12]", "edx");
		vm_configure_fastcall("eax", "ebx", "ecx", "edx");
		//vm_configure_loop("ecx");
		//vm_configure_copy("esi", "edi");
		vm_configure_ret("eax");
		// TODO: do the same for fpregs and mmregs
		if (init) // XXX
			vm_arch_x86_init();
		break;
	case ARCH_MIPS:
#if 0
		vm_nregs    = vm_arch_mips_nregs;
		vm_regs     = vm_arch_mips_regs;
		vm_regs_str = vm_arch_mips_regs_str;
#endif
		// TODO: do the same for fpregs and mmregs
		if (init)
			vm_arch_mips_init();
		break;
		//vm_regs = NULL;
	}
	return 0;
}


int vm_eval_cmp(const char *str)
{
	int len;
	char *p, *ptr;
	for(;*str==' ';str=str+1);
	len = strlen(str)+1;
	ptr = alloca(len);
	memcpy(ptr, str, len);
	p = strchr(ptr, ',');
	if (!p) p = strchr(ptr, ' ');
	if (p) {
		vm_reg_set(vm_cpu.zf,(vm_get_math(ptr)-vm_get_math(p+1)));
		p='\0';
		return 0;
	}
	return 1;
}

int vm_eval_eq(const char *str, const char *val)
{
	char *p;
	u8 buf[64];
	ut64 _int8  = 0;
	ut16 _int16 = 0;
	ut32 _int32 = 0;
	ut64 _int64 = 0;
	for(;*str==' ';str=str+1);
	for(;*val==' ';val=val+1);

	/* STORE */
	if (*str=='[') {
		// USE MMU
		// [foo] = 33, [reg] = 33
		if (*val=='[') {
			// [0x804800] = [0x30480]
			u8 data[8];
			ut64 off = vm_get_math(val+1);
			p = strchr(val+1,':');
			// TODO: support for size 8:addr
			// if (strchr(val, ':')) ..

			if (p) {
				int size = atoi(val+1);
				off = vm_get_math(p+1);
				switch(size) {
				case 8:
					vm_mmu_read(off, buf, 1);
					vm_mmu_write(off, buf, 1);
					break;
				case 16:
					vm_mmu_read(off, buf, 2);
					vm_mmu_write(off, buf, 2);
					break;
				case 64:
					vm_mmu_read(off, buf, 8);
					vm_mmu_write(off, buf, 8);
					break;
				default:
					vm_mmu_read(off, buf, 4);
					vm_mmu_write(off, buf, 4);
				}
			} else {
				vm_mmu_read(off, (u8*)&_int32, 4);
				//off = vm_get_math(val);
				vm_mmu_write(off, (const ut8*)&_int32, 4);
			}
		} else {
			// [0x804800] = eax
			// use ssssskcvtgvmu
			ut64 off = vm_get_math(str+1);
			// XXX support 64 bits here
			ut32 v = (ut32)vm_get_math(val); // TODO control endian
			p = strchr(str+1,':');
			eprintf("   ;==> [0x%08llx] = %x  ((%s))\n", off, v, str+1);

			if (p) {
				int size = atoi(val+1);
				off = vm_get_math(p+1);
				printf(" write size: %d\n", size);
				switch(size) {
				case 8: vm_mmu_write(off, buf, 1);
					break;
				case 16: vm_mmu_write(off, buf, 2);
					break;
				case 64: vm_mmu_write(off, buf, 8);
					break;
				default:
					vm_mmu_write(off, buf, 4);
				}
			} else {
				printf("   ; write %x @ 0x%08llx\n", v, off);
				vm_mmu_write(off, (u8*)&v, 4);
			}
		}
	} else {
	/* LOAD */
		// USE REG
		// reg = [foo] , reg = 33
		if (*val=='[') {
			// use mmu
			u8 data[8];
			ut64 off;
			ut32 _int32 = 0;
			p = strchr(val+1,']');
			if (p)
				*p='\0';
			p = strchr(val+1,':');
			if (p) {
				int size = atoi(val+1);
				off = vm_get_math(p+1);
				switch(size) {
				case 8:
					vm_mmu_read(off, (u8*)&_int8, 1);
					vm_reg_set(str, (ut64)_int8);
					break;
				case 16:
					vm_mmu_read(off, (u8*)&_int16, 2);
					vm_reg_set(str, (ut64)_int16);
					break;
				case 64:
					vm_mmu_read(off, (u8*)&_int64, 8);
					vm_reg_set(str, (ut64)_int64);
					break;
				default:
					vm_mmu_read(off, (u8*)&_int32, 4);
					vm_reg_set(str, (ut64)_int32);
				}
			} else {
 				off = vm_get_math(val+1);
				vm_mmu_read(off, (u8*)&_int32, 4);
				vm_reg_set(str, (ut64)_int32);
			}
		} else vm_reg_set(str, vm_get_math(val));
	}
	return 0;
}

int vm_eval_single(const char *str)
{
	char *ptr, *eq;
	char buf[128];
	int i, len;
	int op;

//	if (log)
	eprintf("   ; %s\n", str);
	for(;str&&str[0]==' ';str=str+1);
	len = strlen(str)+1;
	ptr = alloca(len);
	memcpy(ptr, str, len);
	for(i=0;ptr[i];i++) {
		if (ptr[i]==' ')
			str_cpy(ptr+i, ptr+i+1);
	}
	
	eq = strchr(ptr, '=');
	if (eq) {
		eq[0]='\0';
		op = eq[-1];
		switch(op) {
		case '+':
		case '-':
		case '*':
		case '/':
		case '&':
		case '^':
		case '%':
		case '|':
		case '<':
		case '>':
			eq[-1]='\0';
			snprintf(buf, 127, "%s%c%s", ptr, op, eq+1); //eq[-1], eq+1);
			for(i=0;ptr[i];i++) if (ptr[i]==' '){ptr[i]='\0';break;}
			printf("BUF(%s)(%s)\n", ptr, buf);
			vm_eval_eq(ptr, buf);
			break;
		case ' ':
			i=-1; do { eq[i--]='\0'; } while(eq[i]==' ');
		default:
			//printf("EQ(%s)(%s)\n", ptr, eq+1);
			vm_eval_eq(ptr, eq+1);
		}
		eq[0]='=';
	} else {
		eprintf("Unknown opcode\n");
		if (!memcmp(ptr, "if ", 3)) {
			if (vm_reg_get(ptr+3)!=0)
				return -1;
		} else
		if (!memcmp(ptr, "ifnot ", 6)) {
			if (vm_reg_get(ptr+6)==0)
				return -1;
		} else
		if (!memcmp(ptr, "cmp ", 4)) {
			vm_eval_cmp(str+5);
		} else
		return 0;

		if (!memcmp(ptr, "syscall", 6)) {
			eprintf("TODO: syscall interface not yet implemented\n");
		} else
		if((!memcmp(ptr, "call ", 4))
		|| (!memcmp(ptr, "jmp ", 4))){
			if (ptr[0]=='c')
				vm_stack_push(vm_get_value(vm_cpu.pc));
			printf("CALL(%s)\n", ptr+4);
			vm_reg_set(vm_cpu.pc, vm_get_value(ptr+4));
		} else
		if (!memcmp(ptr, "jz ", 3)){
			if (vm_reg_get(ptr+3)==0)
				vm_reg_set(vm_cpu.pc, vm_get_value(ptr+3));
		} else
		if (!memcmp(ptr, "jnz ", 4)){
			if (vm_reg_get(ptr+4)==0)
				vm_reg_set(vm_cpu.pc, vm_get_value(ptr+4));
		} else
		if (!memcmp(ptr, "push ", 5)) {
			vm_stack_push(vm_get_value (str+5));
		} else
		if (!memcmp(str, "pop ", 4)) {
			vm_stack_pop(str+5);
		} else
		if (!memcmp(ptr, "ret", 3)) {
			vm_stack_pop(vm_cpu.pc);
			printf("RET (%x)\n", vm_cpu.pc);
		} else
			eprintf("Unknown opcode\n");
	}
	return 0;
}

int vm_eval(const char *str)
{
	char *next, *ptr, *p;
	int ret, len = strlen(str)+1;

	ptr = alloca(len);
	memcpy(ptr, str, len);
	vm_mmu_real(config_get_i("vm.realio"));

#if 0
	.int32 eax alias-get alias-set
	.alias eax get set
#endif
	do {
		next = strchr(ptr, ',');
		if (next) {
			next[0]='\0';
			ret = vm_eval_single(ptr);
			if (ret == -1)
				return 0;
			next[0]=',';
			ptr = next +1;
		} else {
		}
	} while(next);
	vm_eval_single(ptr);

	return 1;
}

int vm_eval_file(const char *str)
{
	char buf[1024];
	FILE *fd = fopen(str, "r");
	if (fd) {
		while(!feof(fd)) {
			*buf='\0';
			fgets(buf, 1023, fd);
			if (*buf) {
				buf[strlen(buf)-1]='\0';
				//vm_eval(buf);
				vm_op_eval(buf);
			}
		}
		fclose(fd);
		return 1;
	}
	return 0;
}

/* emulate n opcodes */
int vm_emulate(int n)
{
	ut64 pc;
	char str[128];
	u8 buf[128];
	int opsize;
	int op = config_get_i("asm.pseudo");
	struct aop_t aop;

	printf("Emulating %d opcodes\n", n);
	///vm_init(1);
	vm_mmu_real(config_get_i("vm.realio"));
	vm_import(0);
	config_set("asm.pseudo", "true");
	config_set("asm.syntax", "intel");
	config_set("asm.profile", "simple");
	vm_reg_set(vm_cpu.pc, config.seek);
	while(n--) {
		pc = vm_reg_get(vm_cpu.pc);
	udis_init();
		udis_set_pc(pc);
		vm_mmu_read(pc, buf, 32);
//eprintf("(%02x %02x)\n",  buf[0], buf[1]);
		radare_cmdf("pd 1 @ 0x%08llx", pc);
		pas_aop(config.arch, pc, buf, 32, &aop, str, 1);

		arch_aop(pc, buf, &aop);
		opsize = aop.length;
//eprintf("%llx +  %d (%02x %02x)\n", pc, opsize, buf[0], buf[1]);
		//printf("=> 0x%08llx '%s' (%d)\n", vm_reg_get(vm_cpu.pc), str, opsize);
		vm_reg_set(vm_cpu.pc, vm_reg_get(vm_cpu.pc)+opsize);
		vm_op_eval(str);
	}
	config_set("asm.pseudo", op?"true":"false");
	
#if 0
	eprintf("TODO: vm_emulate\n");
	vm_init(1);
	vm_print();
#endif
// TODO: perform asm-to-pas-eval
// TODO: evaluate string
	return n;
}

#define ALEN 5
int vm_reg_alias_list()
{
	struct vm_reg_t *reg;
	struct list_head *pos;
	int len,space;

	eprintf("Register alias:\n");
	list_for_each(pos, &vm_regs) {
		reg= list_entry(pos, struct vm_reg_t, list);
		if (reg->get == NULL && reg->set == NULL)
			continue;
		len = strlen(reg->name)+1;
		cons_printf("%s:", reg->name);
		if (len>=ALEN) {
			space = ALEN;
			cons_newline();
		} else space = ALEN-len;
		cons_printf("%*cget = %s\n%*cset = %s\n", space, ' ', reg->get, ALEN,' ', reg->set);
	}
	return 0;
}

int vm_reg_alias(const char *name, const char *get, const char *set)
{
	struct vm_reg_t *reg;
	struct list_head *pos;

	list_for_each(pos, &vm_regs) {
		reg = list_entry(pos, struct vm_reg_t, list);
		if (!strcmp(name, reg->name)) {
			free(reg->get);
			reg->get = NULL;
			if (get) reg->get = strdup(get);

			free(reg->set);
			reg->set = NULL;
			if (set) reg->set = strdup(set);
			return 1;
		}
	}
	fprintf(stderr, "Register '%s' not defined.\n", name);
	return 0;
}

int vm_cmd_reg(const char *_str)
{
	struct list_head *pos;
	char *str, *ptr;
	int len;

	len = strlen(_str)+1;
	str = alloca(len);
	memcpy(str, _str, len);

	if (str==NULL ||str[0]=='\0') {
		/* show all registers */
		vm_print(-1);
	} else {
		switch(str[0]) {
		case 'a':
			if (str[1]==' ') {
				char *get,*set;
				get = strchr(str+2, ' ');
				if (get) {
					get[0]='\0';
					get = get+1;
					set = strchr(get, ' ');
					if (set) {
						set[0]='\0';
						set = set +1;
						vm_reg_alias(str+2, get, set);
					}
				}
			} else vm_reg_alias_list();
			break;
		case 't':
			vm_reg_type_list();
			break;
		case '+':
			// add register
			// avr+ eax int32
			for(str=str+1;str&&*str==' ';str=str+1);
			ptr = strchr(str, ' ');
			if (ptr) {
				ptr[0]='\0';
				vm_reg_add(str, vm_reg_type_i(ptr+1), 0);
			} else vm_reg_add(str, VMREG_INT32, 0);
			break;
		case '-':
			// rm register
			// avr- eax
			// avr-*
			for(str=str+1;str&&*str==' ';str=str+1);
			if (str[0]=='*')
				INIT_LIST_HEAD(&vm_regs); // XXX Memory leak
			else vm_reg_del(str);
			break;
		default:
			for(;str&&*str==' ';str=str+1);
			ptr = strchr(str, '=');
			if (ptr) {
				//vm_eval(str);
				vm_op_eval(str);
	#if 0
				/* set register value */
				ptr[0]='\0';
				vm_eval_eq(str, ptr+1);
				ptr[0]='=';
	#endif
			} else {
				if (*str=='.') {
					vm_print(vm_reg_type_i(str+1));
				} else {
					/* show single registers */
					cons_printf("%s = 0x%08llx\n", str, vm_reg_get(str));
				}
			}
		}
	}
	return 0;
}

int vm_op_eval(const char *str)
{
	char *p,*s;
	int i,j,k,len = strlen(str)+256;
	int nargs = 0;
	const char *arg0;

//	eprintf("vmopeval(%s)\n", str);
	p = alloca(len);
	s = alloca(len);
	memcpy(p, str, len);
	memcpy(s, str, len);

	struct list_head *pos;
	nargs = set0word(s);
	arg0 = get0word(s,0);

	list_for_each(pos, &vm_ops) {
		struct vm_op_t *o = list_entry(pos, struct vm_op_t, list);
		if (!strcmp(arg0, o->opcode)) {
			str = o->code;
			p = alloca(strlen(o->code)+128);
			strcpy(p,str);
			for(j=k=0;str[j]!='\0';j++,k++) {
				if (str[j]=='$') {
					j++;
					if (str[j]=='\0') {
						fprintf(stderr, "invalid string\n");
						return 0;
					}
					if (str[j]=='$') {
						/* opcode size */
						if (str[j+1]=='$') {
							struct aop_t aop;
							char w[32];
							j++;
							arch_aop(config.seek, config.block,&aop);
							sprintf(w, "%d", aop.length);
							if (w[0]) {
								strcpy(p+k, w);
								k += strlen(w)-1;
							}
						} else {
							char w[32];
							sprintf(w, "0x%08llx", config.seek);
							if (w[0]) {
								strcpy(p+k, w);
								k += strlen(w)-1;
							}
						}
					}
					if (str[j]>='0' && str[j]<='9') {
						const char *w = get0word(s,str[j]-'0');
						if (w != NULL) {
							strcpy(p+k, w);
							k += strlen(w)-1;
						}
					}
				} else p[k] = str[j];
			}
			p[k]='\0';
		}
	}

	return vm_eval(p);
}

int vm_op_list()
{
	struct list_head *pos;

	eprintf("Oplist\n");
	list_for_each(pos, &vm_ops) {
		struct vm_op_t *o = list_entry(pos, struct vm_op_t, list);
		cons_printf("%s = %s\n", o->opcode, o->code);
	}
	return 0;
}

int vm_cmd_op_help()
{
	cons_printf("avo [op] [expr]\n"
	" \"avo call [esp]=eip+$$$,esp=esp+4,eip=$1\n"
	" \"avo jmp eip=$1\n"
	" \"avo mov $1=$2\n"
	"Note: The prefix '\"' quotes the command and does not parses pipes and so\n");
}

/* TODO : Allow to remove and so on */
int vm_cmd_op(const char *op)
{
	char *cmd, *ptr;
	int len = strlen(op)+1;
	if (*op==' ')
		op = op + 1;
	cmd = alloca(len);
	memcpy(cmd, op, len);
	ptr = strchr(cmd, ' ');
	if (ptr) {
		ptr[0]='\0';
		eprintf("vm: opcode '%s' added\n", cmd);
		vm_op_add(cmd, ptr+1);
	} else vm_cmd_op_help();
	return 0;
}
