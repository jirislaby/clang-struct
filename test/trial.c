#include "../test/trial.h"

#define M struct in_macro { int x; }

M;

void modify_int(int *);

struct X {
	int wr;
	int unused;
	int rd;
	int used;
	int used_ptr;
};

struct my_anon {
	struct {
		int anon_member;
	};
};

struct {
	int anon_member;
} my_anon2;

struct PACKED {
	int used_by_hw;
} __attribute__((packed,aligned(128)));

void fun(struct X *x, struct my_anon *y)
{
	x->wr = 1;
	x->used = my_anon2.anon_member;
	y->anon_member = 1;
}

int fun2(struct X *y, struct X z)
{
	return y->rd + y->used;
}

void fun3(struct X *x)
{
	modify_int(&x->used_ptr);
}

