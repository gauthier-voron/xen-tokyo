/******************************************************************************
 * kernel.c
 * 
 * Copyright (c) 2002-2005 K A Fraser
 */

#include <xen/config.h>
#include <xen/init.h>
#include <xen/lib.h>
#include <xen/errno.h>
#include <xen/monitor.h>
#include <xen/version.h>
#include <xen/sched.h>
#include <xen/paging.h>
#include <xen/nmi.h>
#include <xen/guest_access.h>
#include <xen/hypercall.h>
#include <asm/current.h>
#include <asm/msr.h>
#include <public/nmi.h>
#include <public/version.h>

#ifndef COMPAT

enum system_state system_state = SYS_STATE_early_boot;

int tainted;

xen_commandline_t saved_cmdline;

static void __init assign_integer_param(
    struct kernel_param *param, uint64_t val)
{
    switch ( param->len )
    {
    case sizeof(uint8_t):
        *(uint8_t *)param->var = val;
        break;
    case sizeof(uint16_t):
        *(uint16_t *)param->var = val;
        break;
    case sizeof(uint32_t):
        *(uint32_t *)param->var = val;
        break;
    case sizeof(uint64_t):
        *(uint64_t *)param->var = val;
        break;
    default:
        BUG();
    }
}

void __init cmdline_parse(const char *cmdline)
{
    char opt[100], *optval, *optkey, *q;
    const char *p = cmdline;
    struct kernel_param *param;
    int bool_assert;

    if ( cmdline == NULL )
        return;

    safe_strcpy(saved_cmdline, cmdline);

    for ( ; ; )
    {
        /* Skip whitespace. */
        while ( *p == ' ' )
            p++;
        if ( *p == '\0' )
            break;

        /* Grab the next whitespace-delimited option. */
        q = optkey = opt;
        while ( (*p != ' ') && (*p != '\0') )
        {
            if ( (q-opt) < (sizeof(opt)-1) ) /* avoid overflow */
                *q++ = *p;
            p++;
        }
        *q = '\0';

        /* Search for value part of a key=value option. */
        optval = strchr(opt, '=');
        if ( optval != NULL )
        {
            *optval++ = '\0'; /* nul-terminate the option value */
            q = strpbrk(opt, "([{<");
        }
        else
        {
            optval = q;       /* default option value is empty string */
            q = NULL;
        }

        /* Boolean parameters can be inverted with 'no-' prefix. */
        bool_assert = !!strncmp("no-", optkey, 3);
        if ( !bool_assert )
            optkey += 3;

        for ( param = &__setup_start; param < &__setup_end; param++ )
        {
            if ( strcmp(param->name, optkey) )
            {
                if ( param->type == OPT_CUSTOM && q &&
                     strlen(param->name) == q + 1 - opt &&
                     !strncmp(param->name, opt, q + 1 - opt) )
                {
                    optval[-1] = '=';
                    ((void (*)(const char *))param->var)(q);
                    optval[-1] = '\0';
                }
                continue;
            }

            switch ( param->type )
            {
            case OPT_STR:
                strlcpy(param->var, optval, param->len);
                break;
            case OPT_UINT:
                assign_integer_param(
                    param,
                    simple_strtoll(optval, NULL, 0));
                break;
            case OPT_BOOL:
            case OPT_INVBOOL:
                if ( !parse_bool(optval) )
                    bool_assert = !bool_assert;
                assign_integer_param(
                    param,
                    (param->type == OPT_BOOL) == bool_assert);
                break;
            case OPT_SIZE:
                assign_integer_param(
                    param,
                    parse_size_and_unit(optval, NULL));
                break;
            case OPT_CUSTOM:
                ((void (*)(const char *))param->var)(optval);
                break;
            default:
                BUG();
                break;
            }
        }
    }
}

int __init parse_bool(const char *s)
{
    if ( !strcmp("no", s) ||
         !strcmp("off", s) ||
         !strcmp("false", s) ||
         !strcmp("disable", s) ||
         !strcmp("0", s) )
        return 0;

    if ( !strcmp("yes", s) ||
         !strcmp("on", s) ||
         !strcmp("true", s) ||
         !strcmp("enable", s) ||
         !strcmp("1", s) )
        return 1;

    return -1;
}

/**
 *      print_tainted - return a string to represent the kernel taint state.
 *
 *  'S' - SMP with CPUs not designed for SMP.
 *  'M' - Machine had a machine check experience.
 *  'B' - System has hit bad_page.
 *
 *      The string is overwritten by the next call to print_taint().
 */
char *print_tainted(char *str)
{
    if ( tainted )
    {
        snprintf(str, TAINT_STRING_MAX_LEN, "Tainted: %c%c%c%c",
                 tainted & TAINT_UNSAFE_SMP ? 'S' : ' ',
                 tainted & TAINT_MACHINE_CHECK ? 'M' : ' ',
                 tainted & TAINT_BAD_PAGE ? 'B' : ' ',
                 tainted & TAINT_SYNC_CONSOLE ? 'C' : ' ');
    }
    else
    {
        snprintf(str, TAINT_STRING_MAX_LEN, "Not tainted");
    }

    return str;
}

void add_taint(unsigned flag)
{
    tainted |= flag;
}

extern const initcall_t __initcall_start[], __presmp_initcall_end[],
    __initcall_end[];

void __init do_presmp_initcalls(void)
{
    const initcall_t *call;
    for ( call = __initcall_start; call < __presmp_initcall_end; call++ )
        (*call)();
}

void __init do_initcalls(void)
{
    const initcall_t *call;
    for ( call = __presmp_initcall_end; call < __initcall_end; call++ )
        (*call)();
}

# define DO(fn) long do_##fn

#endif

/*
 * Simple hypercalls.
 */

#ifdef BIGOS_DIRECT_MSR
#  define HYPERCALL_BIGOS_RDMSR         -2
#  define HYPERCALL_BIGOS_WRMSR         -3
#endif

#ifdef BIGOS_MEMORY_MOVE
#  define HYPERCALL_BIGOS_PTSTAT        -4
#  define HYPERCALL_BIGOS_PTSELFMOVE    -5
#  define HYPERCALL_BIGOS_FLUSHTLB      -6
#endif

#ifdef BIGOS_PERF_COUNTING
#  define HYPERCALL_BIGOS_PERF_ENABLE   -7
#  define HYPERCALL_BIGOS_PERF_DISABLE  -8
#endif


#ifdef BIGOS_DIRECT_MSR
#  ifndef COMPAT

static void bigos_rdmsr(void *args)
{
    int ret;
    unsigned long *addr = (unsigned long *) args;
    unsigned long ncpu = smp_processor_id();
    unsigned long *slot = addr + 1 + ncpu;

    ret = rdmsr_safe(*addr, *slot);
    if ( ret )
        *slot = (unsigned long) -1;
}

static void bigos_wrmsr(void *args)
{
    int ret;
    unsigned long *addr = (unsigned long *) args;
    unsigned long *new = addr + 1;
    unsigned long ncpu = smp_processor_id();
    unsigned long *slot = new + 1 + ncpu;
    unsigned long old;

    ret = rdmsr_safe(*addr, old);
    if ( ret )
    {
        *slot = (unsigned long) -1;
        return;
    }

    ret = wrmsr_safe(*addr, *new);
    if ( ret )
    {
        *slot = (unsigned long) -1;
        return;
    }

    *slot = old;
}

#  endif
#endif


DO(xen_version)(int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    switch ( cmd )
    {

#ifdef BIGOS_DIRECT_MSR
#  ifndef COMPAT

    case HYPERCALL_BIGOS_RDMSR:
    {
        static unsigned long maxcpu = 0;
        unsigned long arr[2 + NR_CPUS];
        unsigned long ipi[1 + NR_CPUS];
        unsigned long addr, nrcpus, i;
        cpumask_t cpumask;

        if ( maxcpu == 0 )
            for_each_online_cpu(maxcpu) {}

        if ( copy_from_guest(arr, arg, 2) )
            return -EFAULT;
        addr = arr[0];
        nrcpus = arr[1];
        if ( nrcpus > NR_CPUS )
            return -EFAULT;
        if ( copy_from_guest(arr, arg, 2 + nrcpus) )
            return -EFAULT;

        cpumask_clear(&cpumask);
        for (i=0; i<nrcpus; i++)
        {
            if ( arr[2+i] >= maxcpu )
                return -EFAULT;
            cpumask_set_cpu(arr[2+i], &cpumask);
        }
        ipi[0] = addr;
        on_selected_cpus(&cpumask, bigos_rdmsr, ipi, 1);
        for (i=0; i<nrcpus; i++)
            arr[2+i] = ipi[1 + arr[2+i]];

        if ( copy_to_guest(arg, arr, 2 + nrcpus) )
            return -EFAULT;

        return 0;
    }

    case HYPERCALL_BIGOS_WRMSR:
    {
        static unsigned long maxcpu = 0;
        unsigned long arr[3 + NR_CPUS];
        unsigned long ipi[2 + NR_CPUS];
        unsigned long addr, new, nrcpus, i;
        cpumask_t cpumask;

        if ( maxcpu == 0 )
            for_each_online_cpu(maxcpu) {}

        if ( copy_from_guest(arr, arg, 3) )
            return -EFAULT;
        addr = arr[0];
        new = arr[1];
        nrcpus = arr[2];
        if ( nrcpus > NR_CPUS )
            return -EFAULT;
        if ( copy_from_guest(arr, arg, 3 + nrcpus) )
            return -EFAULT;

        cpumask_clear(&cpumask);
        for (i=0; i<nrcpus; i++)
        {
            if ( arr[3+i] >= maxcpu )
                return -EFAULT;
            cpumask_set_cpu(arr[3+i], &cpumask);
        }
        ipi[0] = addr;
        ipi[1] = new;
        on_selected_cpus(&cpumask, bigos_wrmsr, ipi, 1);
        for (i=0; i<nrcpus; i++)
            arr[3+i] = ipi[2 + arr[3+i]];

        if ( copy_to_guest(arg, arr, 3 + nrcpus) )
            return -EFAULT;

        return 0;
    }

#  endif
#endif

#ifdef BIGOS_MEMORY_MOVE
    /*
     * Special case for manual triggering
     * Should not be in any definitive version
     * The dom0 counterpart is in tools/trigger/
     * TODO: remove as soon as possible
     */
    case HYPERCALL_BIGOS_PTSTAT:
    {
        unsigned long i, tot, node, sum4k, sum2m, sum1g;
        unsigned long last_gfn, last_node, last_order;
        unsigned long domid;
        unsigned int order;
        struct domain *d;
        struct p2m_domain *p2m;
        p2m_type_t t;
        p2m_access_t a;
        unsigned long mfn;

        if ( copy_from_guest(&domid, arg, 1) )
            return -EFAULT;

        d = get_domain_by_id(domid);
        if ( d == NULL )
        {
            printk("Domain %lu unavailable\n", domid);
            return -1;
        }
        if ( d->guest_type != guest_type_hvm )
        {
            printk("Domain %lu is not HVM\n", domid);
            put_domain(d);
            return -1;
        }

        tot = d->tot_pages;
        p2m = p2m_get_hostp2m(d);

        last_gfn = 0;
        last_node = ~0ul;
        last_order = ~0ul;

        sum4k = sum2m = sum1g = 0;

        printk("%-10s %-10s %-5s %-5s\n", "from", "to", "node", "order");
        for (i=0; i<tot; i++)
        {
            mfn = mfn_x(p2m->get_entry(p2m, i, &t, &a, 0, &order));
            if ( mfn == INVALID_MFN )
                continue;
            node = phys_to_nid(mfn << PAGE_SHIFT);

            if ( order == PAGE_ORDER_4K )
                sum4k++;
            else if ( order == PAGE_ORDER_2M )
                sum2m++;
            else if ( order == PAGE_ORDER_1G )
                sum1g++;

            if ( unlikely(node != last_node || order != last_order) )
            {
                if ( likely(last_node != ~0ul || last_order != ~0ul) )
                    printk("0x%-8lx 0x%-8lx %-5lu %-5lu\n",
                           last_gfn, i-1, last_node, last_order);
                last_gfn = i;
                last_node = node;
                last_order = order;
            }
        }

        put_domain(d);

        printk("sum 4k = %lu\n", sum4k);
        printk("sum 2m = %lu\n", sum2m);
        printk("sum 1g = %lu\n", sum1g);

        return 0;
    }

    case HYPERCALL_BIGOS_PTSELFMOVE:
    {
        unsigned long arr[2];
        unsigned long domid, gfn, node;
        struct domain *d;
        struct p2m_domain *p2m;
        p2m_type_t t;
        p2m_access_t a;
        unsigned long mfn;

        if ( copy_from_guest(&arr, arg, 2) )
            return -EFAULT;
        domid = arr[0];
        gfn = arr[1];

        d = get_domain_by_id(domid);
        if ( d == NULL )
        {
            printk("Domain %lu unavailable\n", domid);
            return -1;
        }
        if ( d->guest_type != guest_type_hvm )
        {
            printk("Domain %lu is not HVM\n", domid);
            put_domain(d);
            return -1;
        }

        p2m = p2m_get_hostp2m(d);

        mfn = mfn_x(p2m->get_entry(p2m, gfn, &t, &a, 0, NULL));
        if ( mfn == INVALID_MFN )
        {
            put_domain(d);
            return -1;
        }

        node = phys_to_nid(mfn << PAGE_SHIFT);
        if ( memory_move(d, gfn, node) )
        {
            put_domain(d);
            return -1;
        }

        put_domain(d);
        return 0;
    }

    case HYPERCALL_BIGOS_FLUSHTLB:
    {
        flush_tlb_all();
        return 0;
    }
#endif /* BIGOS_MEMORY_MOVE */

#ifdef BIGOS_PERF_COUNTING
    case HYPERCALL_BIGOS_PERF_ENABLE:
    {
        printk("Enabling perf counting\n");
        return start_monitoring();
    }

    case HYPERCALL_BIGOS_PERF_DISABLE:
    {
        printk("Disable perf counting\n");
        stop_monitoring();
        return 0;
    }
#endif /* BIGOS_PERF_COUNTING */

    case XENVER_version:
    {
        return (xen_major_version() << 16) | xen_minor_version();
    }

    case XENVER_extraversion:
    {
        xen_extraversion_t extraversion;
        safe_strcpy(extraversion, xen_extra_version());
        if ( copy_to_guest(arg, extraversion, ARRAY_SIZE(extraversion)) )
            return -EFAULT;
        return 0;
    }

    case XENVER_compile_info:
    {
        struct xen_compile_info info;
        safe_strcpy(info.compiler,       xen_compiler());
        safe_strcpy(info.compile_by,     xen_compile_by());
        safe_strcpy(info.compile_domain, xen_compile_domain());
        safe_strcpy(info.compile_date,   xen_compile_date());
        if ( copy_to_guest(arg, &info, 1) )
            return -EFAULT;
        return 0;
    }

    case XENVER_capabilities:
    {
        xen_capabilities_info_t info;

        memset(info, 0, sizeof(info));
        arch_get_xen_caps(&info);

        if ( copy_to_guest(arg, info, ARRAY_SIZE(info)) )
            return -EFAULT;
        return 0;
    }
    
    case XENVER_platform_parameters:
    {
        xen_platform_parameters_t params = {
            .virt_start = HYPERVISOR_VIRT_START
        };
        if ( copy_to_guest(arg, &params, 1) )
            return -EFAULT;
        return 0;
        
    }
    
    case XENVER_changeset:
    {
        xen_changeset_info_t chgset;
        safe_strcpy(chgset, xen_changeset());
        if ( copy_to_guest(arg, chgset, ARRAY_SIZE(chgset)) )
            return -EFAULT;
        return 0;
    }

    case XENVER_get_features:
    {
        xen_feature_info_t fi;
        struct domain *d = current->domain;

        if ( copy_from_guest(&fi, arg, 1) )
            return -EFAULT;

        switch ( fi.submap_idx )
        {
        case 0:
            fi.submap = 0;
            if ( VM_ASSIST(d, VMASST_TYPE_pae_extended_cr3) )
                fi.submap |= (1U << XENFEAT_pae_pgdir_above_4gb);
            if ( paging_mode_translate(current->domain) )
                fi.submap |= 
                    (1U << XENFEAT_writable_page_tables) |
                    (1U << XENFEAT_auto_translated_physmap);
            if ( supervisor_mode_kernel )
                fi.submap |= 1U << XENFEAT_supervisor_mode_kernel;
            if ( is_hardware_domain(current->domain) )
                fi.submap |= 1U << XENFEAT_dom0;
#ifdef CONFIG_X86
            switch ( d->guest_type )
            {
            case guest_type_pv:
                fi.submap |= (1U << XENFEAT_mmu_pt_update_preserve_ad) |
                             (1U << XENFEAT_highmem_assist) |
                             (1U << XENFEAT_gnttab_map_avail_bits);
                break;
            case guest_type_pvh:
                fi.submap |= (1U << XENFEAT_hvm_safe_pvclock) |
                             (1U << XENFEAT_supervisor_mode_kernel) |
                             (1U << XENFEAT_hvm_callback_vector);
                break;
            case guest_type_hvm:
                fi.submap |= (1U << XENFEAT_hvm_safe_pvclock) |
                             (1U << XENFEAT_hvm_callback_vector) |
                             (1U << XENFEAT_hvm_pirqs);
                break;
            }
#endif
            break;
        default:
            return -EINVAL;
        }

        if ( copy_to_guest(arg, &fi, 1) )
            return -EFAULT;
        return 0;
    }

    case XENVER_pagesize:
    {
        return (!guest_handle_is_null(arg) ? -EINVAL : PAGE_SIZE);
    }

    case XENVER_guest_handle:
    {
        if ( copy_to_guest(arg, current->domain->handle,
                           ARRAY_SIZE(current->domain->handle)) )
            return -EFAULT;
        return 0;
    }

    case XENVER_commandline:
    {
        if ( copy_to_guest(arg, saved_cmdline, ARRAY_SIZE(saved_cmdline)) )
            return -EFAULT;
        return 0;
    }
    }

    return -ENOSYS;
}

DO(nmi_op)(unsigned int cmd, XEN_GUEST_HANDLE_PARAM(void) arg)
{
    struct xennmi_callback cb;
    long rc = 0;

    switch ( cmd )
    {
    case XENNMI_register_callback:
        rc = -EFAULT;
        if ( copy_from_guest(&cb, arg, 1) )
            break;
        rc = register_guest_nmi_callback(cb.handler_address);
        break;
    case XENNMI_unregister_callback:
        rc = unregister_guest_nmi_callback();
        break;
    default:
        rc = -ENOSYS;
        break;
    }

    return rc;
}

DO(vm_assist)(unsigned int cmd, unsigned int type)
{
    return vm_assist(current->domain, cmd, type);
}

DO(ni_hypercall)(void)
{
    /* No-op hypercall. */
    return -ENOSYS;
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
