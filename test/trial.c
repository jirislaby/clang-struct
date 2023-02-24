void modify_int(int *);

struct X {
	int wr;
	int unused;
	int rd;
	int used;
	int used_ptr;
};

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

