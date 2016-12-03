
#include <stdio.h>

int hoge(int n)
{
	int i, s;
	s = 0;
	i = 0;
	while (i < n) {
		s += i + 1;
		i++;
	}
	return s;
}

int main()
{
	printf("%d\n", hoge(10));
	return 0;
}


