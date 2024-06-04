#define expand(n, MEMBERS...) \
        union { \
                struct { MEMBERS }; \
                struct { MEMBERS } named; \
        }

struct X {
	expand(x,
	    int a;
	);
};
