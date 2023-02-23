struct X {
	int wr;
	int unused;
	int rd;
	int used1, used2;
};

void fun(struct X *x)
{
	x->wr = 1;
	x->used1 = 1;
	x->used2 = 0;
}

int fun2(struct X *y, struct X z)
{
	return y->rd + y->used1 + z.used2;
}
