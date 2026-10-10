#include "git-compat-util.h"
#include "setup.h"

/* git v2.53.0 setup.c:2074 */
void sanitize_stdfds(void)
{
	int fd = xopen("/dev/null", O_RDWR);
	while (fd < 2)
		fd = xdup(fd);
	if (fd > 2)
		close(fd);
}
