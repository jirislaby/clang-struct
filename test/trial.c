#include "../test/trial.h"

void modify_int(int *);

struct X {
	int wr;
	int unused;
	int rd;
	int used;
	int used_ptr;
};

struct PACKED {
	int used_by_hw;
} __attribute__((packed,aligned(128)));

void fun(struct X *x)
{
	x->wr = 1;
	x->used = 1;
}

int fun2(struct X *y, struct X z)
{
	return y->rd + y->used;
}

void fun3(struct X *x)
{
	modify_int(&x->used_ptr);
}

