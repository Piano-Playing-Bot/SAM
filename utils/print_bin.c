#define AIL_TYPES_IMPL
#define AIL_FS_IMPL
#include <stdio.h>
#include "ail.h"
#include "ail_fs.h"

u64 ctoi(char c)
{
	if      (c >= '0' && c <= '9') return c - '0';
	else if (c >= 'a' && c <= 'z') return c - 'a' + 10;
	else if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
	else {
		printf("%c\n", c);
		AIL_UNREACHABLE();
	}
}

u64 based_atoi(char *str, u64 base)
{
	// printf("based_atoi: \"%s\"\n", str);
	u64 res = 0;
	while (*str) {
		// printf("based_atoi: '%c'\n", *str);
		res = res*base + ctoi(*str);
		str++;
	}
	// printf("based_atoi: %lld\n", res);
	return res;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		printf("Not enough input arguments.\n");
		printf("Usage:\n");
		printf("%s <file> <from> <to>", argv[0]);
		return 1;
	}
	char *fname = argv[1];
	u64 from = based_atoi(argv[2], 16);
	u64 to   = based_atoi(argv[3], 16);
	u64 fsize;
	char *file = ail_fs_read_entire_file(fname, &fsize);
	printf("File: %s, size: %lld, from: %lld, to: %lld\n", fname, fsize, from, to);
	for (u64 i = AIL_MIN(from, fsize); i < AIL_MIN(to, fsize); i++) {
		printf("%2x ", (u8)file[i]);
	}
	// printf("\n");
	return 0;
}