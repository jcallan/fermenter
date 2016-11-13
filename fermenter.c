#include <stdio.h>

#define VERSION_MAJOR		0
#define VERSION_MINOR		1

#define TIME_INCREMENT_S	60

int main(int argc, const char *argv[])
{
	int i;
	
	printf("%s version %u.%u\n", argv[0], VERSION_MAJOR, VERSION_MINOR);
	for (i = 1; i < argc; ++i)
	{
		if (argv[i][0] == '-')
		{
			switch (argv[i][1])
			{
			case 'h':
				printf("Usage:\n%s [-h]\n\n-h: show this help information\n\n", argv[0]);
				return 1;
				break;
			default:
				printf("Bad flag -%c!\n", argv[i][1]);
				return 2;
				break;
			}
		}
	}
	
	return 0;
}
