#include <mctx.h>

struct A
{
	int x, y;
};

int main()
{
	dixelu::mctx m;
	m["s"] = A{1, 4};
	m["asdf"] = A{234254,34535};

	return 0;
}