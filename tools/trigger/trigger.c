#include <stdlib.h>
#include <stdio.h>

#include <xenctrl.h>
#include <xenguest.h>
#include <xc_private.h>


int main(void)
{
    int ret;
    xc_interface *xch = xc_interface_open(0, 0, 0);

    DECLARE_HYPERCALL;

    if (xch == NULL)
        goto err;

    hypercall.op = __HYPERVISOR_xen_version;
    hypercall.arg[0] = -1;

    ret = do_xen_hypercall(xch, &hypercall);
    xc_interface_close(xch);

    if (ret != 0)
        goto err;
 out:
    return ret;
 err:
    ret = -1;
    goto out;
}
