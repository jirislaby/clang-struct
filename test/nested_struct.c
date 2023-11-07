// SQL: SELECT count(id) FROM use_view WHERE struct = 'A' AND member = 'used' AND src LIKE '%/nested_struct.c';
// EXPECT: ^1$

struct A {
	int used;
};

struct B {
	struct A a;
} b;

int fun()
{
	return b.a.used;
}
