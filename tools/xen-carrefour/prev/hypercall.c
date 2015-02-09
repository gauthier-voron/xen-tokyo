#include <stdio.h>
#include "xc_private.h"


#define HYPERCALL_BIGOS_RDMSR         -2
#define HYPERCALL_BIGOS_WRMSR         -3
#define HYPERCALL_BIGOS_CFR_WRITE     -14

static int hypercall(unsigned long command, unsigned long *args, int *ret)
{
	xc_interface *xch = xc_interface_open(0, 0, 0);
	int _ret;
	
	DECLARE_HYPERCALL;
	
	if (xch == NULL)
		    return -1;
	if (!ret)
		    ret = &_ret;
	
	hypercall.op = __HYPERVISOR_xen_version;
	hypercall.arg[0] = command;
	hypercall.arg[1] = (unsigned long) args;
	
	*ret = do_xen_hypercall(xch, &hypercall);
	
	xc_interface_close(xch);

	return 0;
}

void wrmsr(unsigned long addr, unsigned long value, int cpu)
{
	unsigned long cmd[4] = { addr, value, 1, cpu };
	int ret;

	if (hypercall(HYPERCALL_BIGOS_WRMSR, cmd, &ret) != 0)
		fprintf(stderr, "ERROR: cannot hypercall for WRMSR(%lx, %lx, "
			"%d)\n", addr, value, cpu);
	if (ret != 0)
		fprintf(stderr, "ERROR: hypercall failed for WRMSR(%lx, %lx, "
			"%d)\n", addr, value, cpu);
}

unsigned long rdmsr(unsigned long addr, int cpu)
{
	unsigned long cmd[3] = { addr, 1, cpu };
	int ret;
	
	if (hypercall(HYPERCALL_BIGOS_RDMSR, cmd, &ret) != 0)
		fprintf(stderr, "ERROR: cannot hypercall for RDMSR(%lx, %d)\n",
			addr, cpu);
	if (ret != 0)
		fprintf(stderr, "ERROR: hypercall failed for RDMSR(%lx, %d)\n",
			addr, cpu);
	else
		return cmd[2];

	return 0;
}

double musage(void)
{
	xc_interface *xch = xc_interface_open(0, 0, 0);
	DECLARE_SYSCTL;
	
	if (xch == NULL)
		return -1.0;
	sysctl.cmd = XEN_SYSCTL_physinfo;
	if (do_sysctl(xch, &sysctl) < 0)
		return -1.0;

	return 100 - 100.0 * ((double) sysctl.u.physinfo.free_pages)
		/ ((double) sysctl.u.physinfo.total_pages);
}

void xen_carrefour_send(const char *str, unsigned long count)
{
	unsigned long *cmd;
	int ret;

	cmd = alloca(sizeof(count) + count + 1);
	cmd[0] = count;
	memcpy(&cmd[1], str, count);
	((char *) cmd)[sizeof(count) + count] = '\0';

	if (hypercall(HYPERCALL_BIGOS_CFR_WRITE, cmd, &ret) != 0)
		fprintf(stderr, "ERROR: cannot hypercall for WRITE(%s, %lu)\n",
			((char *) &cmd[1]), count);
	if (ret != count)
		fprintf(stderr, "ERROR: hypercall failed for WRITE(%s, %lu) "
			"-> %d\n",
			((char *) &cmd[1]), count, ret);
}
