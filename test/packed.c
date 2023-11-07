// SQL: SELECT count(id) FROM use_view WHERE struct = 'A' AND member = 'used' AND src LIKE '%/packed.c';
// EXPECT: ^0$

struct A {
	int used_by_hw;
} __attribute__((packed));
