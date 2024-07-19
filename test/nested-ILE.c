struct B {
	int Bunused;
	union {
		int Bi;
	};
};

struct A {
	struct B Ab;
};

void do_wr(struct A *a);

void fun(int i)
{
	struct A a;

	a = (struct A) {
		.Ab.Bi = i,
	};

	do_wr(&a);
}
