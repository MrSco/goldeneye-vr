#include "gevr_bean_logic.h"

#include <stdio.h>

int main(void)
{
	int r = gevrBeanLogicSelfTest();
	if (r) {
		fprintf(stderr, "gevr bean logic self-test failed: %d\n", r);
		return r;
	}
	printf("gevr bean logic self-test ok\n");
	return 0;
}
