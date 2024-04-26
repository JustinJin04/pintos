#define P 17
#define Q 14
#define F (1 << Q)

typedef int fixed_t;

#define CONVERT_TO_FIXED(n) ((n) * F)
#define CONVERT_TO_INT_ZERO(x) ((x) / F)
#define CONVERT_TO_INT_NEAREST(x) ((x) >= 0 ? ((x) + F / 2) / F : ((x) - F / 2) / F)
#define ADD_FIXED_FIXED(x, y) ((x) + (y))
#define SUB_FIXED_FIXED(x, y) ((x) - (y))
#define ADD_FIXED_INT(x, n) ((x) + (n) * F)
#define SUB_FIXED_INT(x, n) ((x) - (n) * F)
#define MUL_FIXED_FIXED(x, y) (((int64_t) (x)) * (y) / F)
#define MUL_FIXED_INT(x, n) ((x) * (n))
#define DIV_FIXED_FIXED(x, y) (((int64_t) (x)) * F / (y))
#define DIV_FIXED_INT(x, n) ((x) / (n))