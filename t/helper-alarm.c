/*
 * alarm SECONDS COMMAND...: COMMAND under an alarm, so a deadlock fails
 * instead of hanging; git's t/helper replaces Perl one-liners the same way.
 */
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	alarm(atoi(argv[1]));
	execvp(argv[2], argv + 2);
	return 127;
}
