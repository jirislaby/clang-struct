struct A {
	union {
		int Ai;
	};
	union {
		int Bi;
	};
};

void do_wr(struct A *a);

void fun(int i)
{
	struct A a = {
		.Ai = i,
	};

	do_wr(&a);
}
