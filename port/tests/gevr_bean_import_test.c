/* Driver for test_bean_import.py.
 *   gevr_bean_import_test import <copy> <dest>   imports, prints "<count> <message>"
 *   gevr_bean_import_test wanted                 maps each stdin line, "-" when unwanted
 */
#include "gevr_bean_import.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
	char line[1024];
	char out[512];
	char msg[256];

	if (argc == 2 && !strcmp(argv[1], "wanted")) {
		while (fgets(line, sizeof(line), stdin)) {
			line[strcspn(line, "\r\n")] = '\0';
			printf("%s\n", gevrBeanImportWanted(line, out, sizeof(out)) ? out : "-");
		}
		return 0;
	}

	if (argc == 4 && !strcmp(argv[1], "import")) {
		const int n = gevrBeanImportStream(fopen(argv[2], "rb"), argv[3], msg, sizeof(msg));
		printf("%d %s\n", n, msg);
		return n < 0;
	}

	fprintf(stderr, "usage: %s import <copy> <dest> | wanted\n", argv[0]);
	return 2;
}
