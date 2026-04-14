#include "ADT_L2_NLINE_U1553_bcrt.h"

int main()
{
	int s = rt1_init();
	if (s != 0) {
		return -1;
	}

	char msg1[] = "Hello World";
	s = rt1_send(msg1, sizeof(msg1));

	char msg2[100];
	s = rt1_recv(msg2, sizeof(msg2));

	rt1_close();

	return 0;
}
