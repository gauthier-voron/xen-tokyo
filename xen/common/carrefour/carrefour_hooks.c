#include <xen/carrefour/carrefour_main.h>
#include <xen/domain.h>
#include <xen/lib.h>
#include <xen/mm.h>
#include <xen/sched.h>


/* struct carrefour_options_t carrefour_default_options = { */
/*    .page_bouncing_fix_4k = 0, */
/*    .page_bouncing_fix_2M = 0, */
/*    .async_4k_migrations  = 0, */
/*    .throttle_4k_migrations_limit = 0, */
/*    .throttle_2M_migrations_limit = 0, */
/* }; */

/* struct carrefour_options_t carrefour_options; */
/* struct carrefour_hook_stats_t carrefour_hook_stats; */

/* static u64 iteration_length_cycles = 0; */

/* static unsigned disable_4k_migrations_globally = 0; */
/* static unsigned disable_2M_migrations_globally = 0; */

/* int is_huge_addr_sloppy (int domain, unsigned long addr) { */
/*    struct task_struct *task; */
/*    struct mm_struct *mm = NULL; */
/*    struct vm_area_struct *vma; */

/*    pgd_t *pgd; */
/*    pud_t *pud; */
/*    pmd_t *pmd; */

/*    int is_huge = -1; */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       mm = get_task_mm(task); */
/*    } */
/*    rcu_read_unlock(); */
/*    if (!mm) */
/*       goto out; */

/*    down_read(&mm->mmap_sem); */

/*    vma = find_vma(mm, addr); */
/*    if (!vma || addr < vma->vm_start) { */
/*       goto out_locked; */
/*    } */
 
/*    pgd = pgd_offset(mm, addr); */
/*    if (!pgd_present(*pgd )) { */
/*       goto out_locked; */
/*    } */

/*    pud = pud_offset(pgd, addr); */
/*    if(!pud_present(*pud)) { */
/*       goto out_locked; */
/*    } */

/*    pmd = pmd_offset(pud, addr); */
/*    if (!pmd_present(*pmd )) { */
/*       goto out_locked; */
/*    } */

/*    if(pmd_trans_huge(*pmd)) { */
/*       is_huge = 1; */
/*    } */
/*    else if(unlikely(is_vm_hugetlb_page(vma))) { */
/*       is_huge = 1; */
/*    } */
/*    else { */
/*       is_huge = 0; */
/*    } */

/* out_locked: */
/*    up_read(&mm->mmap_sem); */
/*    mmput(mm); */

/* out: */
/*    return is_huge; */
/* } */
/* EXPORT_SYMBOL(is_huge_addr_sloppy); */

/* int page_status_for_carrefour(int pid, unsigned long addr, int * already_treated, int * huge) { */
/*    struct task_struct *task; */
/*    struct mm_struct *mm = NULL; */
/*    struct vm_area_struct *vma; */
/*    struct page * page; */
/*    int ret = -1; */
/*    int err; */
/*    int flags = 0; */

/*    *already_treated = *huge = 0; */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       mm = get_task_mm(task); */
/*    } */
/*    rcu_read_unlock(); */
/*    if (!mm) */
/*       return -1; */

/*    down_read(&mm->mmap_sem); */

/*    vma = find_vma(mm, addr); */
/*    if (!vma || addr < vma->vm_start) */
/*       goto out_locked; */

/*    if (!is_vm_hugetlb_page(vma)) { */
/*       flags |= FOLL_GET; */
/*    } */
 
/*    page = follow_page(vma, addr, flags); */

/*    err = PTR_ERR(page); */
/*    if (IS_ERR(page) || !page) */
/*       goto out_locked; */

/*    ret = 0; */

/*    if(page->stats.nr_migrations) { // Page has been migrated already once */
/*       *already_treated = 1; */
/*    } */
/*    else if(PageReplication(page)) { */
/*       *already_treated = 1; */
/*    } */
 
/*    if (is_vm_hugetlb_page(vma)) { */
/*       if(PageHuge(page)) { */
/*          *huge = 1; */
/*       } */
/*       else { */
/*          DEBUG_PANIC("[WARNING] How could it be possible ?\n"); */
/*       } */
/*    } */

/*    if(transparent_hugepage_enabled(vma) && (PageTransHuge(page))) { */
/*       *huge = 2; */
/*    } */

/*    if(flags & FOLL_GET) { */
/*       put_page(page); */
/*    } */

/* out_locked: */
/*    //printk("[Core %d, PID %d] Releasing mm lock (0x%p)\n", smp_processor_id(), pid, &mm->mmap_sem); */
/*    up_read(&mm->mmap_sem); */
/*    mmput(mm); */

/*    return ret; */
/* } */
/* EXPORT_SYMBOL(page_status_for_carrefour); */

/* void reset_carrefour_stats (void) { */
/*    int i = 0; */
/*    write_lock(&carrefour_hook_stats_lock); */
/*    memset(&carrefour_hook_stats, 0, sizeof(struct carrefour_hook_stats_t)); */
/*    for(i = 0; i < num_online_cpus(); i++) { */
/*       struct carrefour_migration_stats_t * stats_cpu = per_cpu_ptr(&carrefour_migration_stats, i); */
/*       memset(stats_cpu, 0, sizeof(struct carrefour_migration_stats_t)); */
/*    } */

/*    disable_2M_migrations_globally = 0; */
/*    disable_4k_migrations_globally = 0; */

/*    write_unlock(&carrefour_hook_stats_lock); */
/* } */
/* EXPORT_SYMBOL(reset_carrefour_stats); */


/* void reset_carrefour_hooks (void) { */
/*    carrefour_options = carrefour_default_options; */
/*    reset_carrefour_stats(); */
/* } */
/* EXPORT_SYMBOL(reset_carrefour_hooks); */


/* void configure_carrefour_hooks(struct carrefour_options_t options) { */
/*    carrefour_options = options; */
/* } */
/* EXPORT_SYMBOL(configure_carrefour_hooks); */

/* struct carrefour_options_t get_carrefour_hooks_conf(void) { */
/*    return carrefour_options; */
/* } */
/* EXPORT_SYMBOL(get_carrefour_hooks_conf); */


/* struct carrefour_hook_stats_t get_carrefour_hook_stats(void) { */
/*    struct carrefour_hook_stats_t stats; */
/*    int i; */

/*    write_lock(&carrefour_hook_stats_lock); */
/*    stats = carrefour_hook_stats; */
/*    stats.time_spent_in_migration_4k = 0; */
/*    stats.time_spent_in_migration_2M = 0; */
/*    for(i = 0; i < num_online_cpus(); i++) { */
/*       struct carrefour_migration_stats_t * stats_cpu = per_cpu_ptr(&carrefour_migration_stats, i); */
/*       stats.time_spent_in_migration_4k += stats_cpu->time_spent_in_migration_4k; */
/*       stats.time_spent_in_migration_2M += stats_cpu->time_spent_in_migration_2M; */

/*       stats.nr_4k_migrations += stats_cpu->nr_4k_migrations; */
/*       stats.nr_2M_migrations += stats_cpu->nr_2M_migrations; */
/*    } */
/*    write_unlock(&carrefour_hook_stats_lock); */

/*    return stats; */
/* } */
/* EXPORT_SYMBOL(get_carrefour_hook_stats); */

int s_migrate_pages(int domain, unsigned long nr_pages, void ** pages,
		    int * nodes, int throttle) {
    unsigned long i;
    struct domain *d = get_domain_by_id(domain);

    for (i=0; i<nr_pages; i++)
	    memory_move(d, ((unsigned long) pages[i]) >> PAGE_SHIFT, nodes[i]);

    put_domain(d);

    return 0;
	
/*    struct task_struct *task; */
/*    struct mm_struct *mm = NULL; */

/*    int i = 0; */
/*    int err = 0; */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       mm = get_task_mm(task); */
/*    } */
/*    rcu_read_unlock(); */

/*    if (!mm) { */
/*       err = -ESRCH; */
/*       goto out_clean; */
/*    } */

/*    down_read(&mm->mmap_sem); */

/*    for(i = 0; i < nr_pages; i++) { */
/*       struct page * page; */
/*       struct vm_area_struct *vma; */

/*       unsigned long addr = (unsigned long) pages[i]; */
/*       int current_node; */

/*       pte_t* pte; */
/*       spinlock_t* ptl; */

/*       vma = find_vma(mm, addr); */
/*       if (!vma || addr < vma->vm_start) { */
/*          continue; */
/*       } */

/*       pte = get_locked_pte_from_va (mm->pgd_master, mm, addr, &ptl); */

/*       if(!pte) { */
/*          continue; */
/*       } */

/*       page = pte_page(*pte); */

/*       if (IS_ERR(page) || !page) { */
/*          pte_unmap_unlock(pte, ptl); */
/*          //DEBUG_WARNING("Cannot migrate a NULL page\n"); */
/*          continue; */
/*       } */

/*       get_page(page); */

/*       /\* Don't want to migrate a replicated page *\/ */
/*       if (PageReplication(page)) { */
/*          pte_unmap_unlock(pte, ptl); */
/*          put_page(page); */
/*          continue; */
/*       } */

/*       if (PageHuge(page) || PageTransHuge(page)) { */
/*          DEBUG_WARNING("[WARNING] What am I doing here ?\n"); */
/*          pte_unmap_unlock(pte, ptl); */
/*          put_page(page); */
/*          continue; */
/*       } */

/*       if(carrefour_options.page_bouncing_fix_4k && (page->stats.nr_migrations >= carrefour_options.page_bouncing_fix_4k)) { */
/*          //DEBUG_WARNING("Page bouncing fix enable\n"); */
/*          pte_unmap_unlock(pte, ptl); */
/*          put_page(page); */
/*          continue; */
/*       } */

/*       current_node = page_to_nid(page); */
/*       if(current_node == nodes[i]) { */
/*          //DEBUG_WARNING("Current node (%d) = destination node (%d) for page 0x%lx\n", current_node, nodes[i], addr); */
/*          pte_unmap_unlock(pte, ptl); */
/*          put_page(page); */
/*          continue; */
/*       } */

/*       if(throttle || !carrefour_options.async_4k_migrations) { */
/*          unsigned allowed = migration_allowed_4k(); */
/*          pte_unmap_unlock(pte, ptl); */

/*          if(allowed && migrate_misplaced_page(page, nodes[i])) { */
/*             //__DEBUG("Migrating page 0x%lx\n", addr); */

/*             // FGAUD */
/*             if(migration_callback) { */
/*                migration_callback(mm, addr);  */
/*             } */
/*          } */
/*          else { */
/*             if(!allowed) */
/*                __DEBUG("migration was not allowed\n"); */
/*             //DEBUG_WARNING("[WARNING] Migration of page 0x%lx failed !\n", addr); */
/*          } */
/*       } */
/*       else { */
/*          pte_t new_pte = *pte; */
/*          pte_unmap_unlock(pte, ptl); */

/*          lock_page(page); */

/*          /\* Confirm the PTE did not while locked *\/ */
/*          spin_lock(ptl); */
/*          if (likely(pte_same(new_pte, *pte))) { */
/*             // TODO: we should lock the page */
/*             page->dest_node = nodes[i]; */

/*             // Make sure that pmd is tagged as "NUMA" */
/*             new_pte = pte_mknuma(new_pte); */
/*             set_pte_at(mm, addr, pte, new_pte); */

/*             /\** FGAUD: We need to flush the TLB, don't we ? **\/ */
/*             flush_tlb_page(vma, addr); */

/*             /\** And make sure to invalid all copies -- TODO: too many flush **\/ */
/*             clear_flush_all_node_copies(mm, vma, addr); */
/*          } */

/*          spin_unlock(ptl); */

/*          unlock_page(page); */
/*          put_page(page); */
/*       } */
/*    } */

/*    up_read(&mm->mmap_sem); */
/*    mmput(mm); */

/* out_clean: */
/*    return err; */
}

/* static struct page *new_page(struct page *p, unsigned long private, int **x) */
/* { */
/*    // private containe the destination node */
/*    return alloc_huge_page_node(page_hstate(p), private); */
/* } */

/* int s_migrate_hugepages(pid_t pid, unsigned long nr_pages, void ** pages, int * nodes) { */
/*    struct task_struct *task; */
/*    struct mm_struct *mm = NULL; */

/*    int i = 0; */
	
/* #if ENABLE_MIGRATION_STATS */
/*    uint64_t start_migr, end_migr, migrated = 0; */
/*    rdtscll(start_migr); */
/* #endif */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       mm = get_task_mm(task); */
/*    } */
/*    rcu_read_unlock(); */

/*    if (!mm) */
/*       return -ESRCH; */

/* 	down_read(&mm->mmap_sem); */

/*    for(i = 0; i < nr_pages; i++) { */
/*       struct page * hpage; */

/*       // Get the current page */
/*       struct vm_area_struct *vma; */
/*       int ret; */

/*       unsigned long addr = (unsigned long) pages[i]; */
/*       int current_node; */

/*       vma = find_vma(mm, addr); */
/*       //if (!vma || pp->addr < vma->vm_start || !vma_migratable(vma)) */
/*       if (!vma || addr < vma->vm_start || !is_vm_hugetlb_page(vma)) */
/*          continue; */

/*       hpage = follow_page(vma, addr, 0); */

/*       if (IS_ERR(hpage) || !hpage) */
/*          continue; */

/*       if(hpage != compound_head(hpage)) { */
/*          DEBUG_WARNING("[WARNING] What's going on ?\n"); */
/*          continue; */
/*       } */

/*       if(! PageHuge(hpage)) { */
/*          DEBUG_WARNING("[WARNING] What am I doing here ?\n"); */
/*          continue; */
/*       } */

/*       current_node = page_to_nid(hpage); */
/*       if(current_node != nodes[i]) { */
/*          // Migrate the page */
/*          if(get_page_unless_zero(hpage)) { */
/*             ret = migrate_huge_page(hpage, new_page, nodes[i], MIGRATE_SYNC); */
/*             put_page(hpage); */

/*             if(ret) { */
/*                printk("[WARNING] Migration of page 0x%lx failed !\n", addr); */
/*             } */
/*             else { */
/* #if ENABLE_MIGRATION_STATS */
/*                INCR_REP_STAT_VALUE(migr_2M_from_to_node[current_node][nodes[i]], 1); */
/*                migrated = 1; */
/* #endif */
/*             } */
/*          } */
/*       } */
/*    } */

/*    up_read(&mm->mmap_sem); */
/*    mmput(mm); */

/* #if ENABLE_MIGRATION_STATS */
/*    rdtscll(end_migr); */
/*    INCR_MIGR_STAT_VALUE(2M, (end_migr - start_migr), migrated); */
/* #endif */
/*    return 0; */
/* } */
/* EXPORT_SYMBOL(s_migrate_hugepages); */

/* // Quick and dirty for now. TODO: update */
/* int move_thread_to_node(pid_t tid, int node) { */
/*    int ret; */
/*    ret = sched_setaffinity(tid, cpumask_of_node(node)); */
/*    sched_setaffinity(tid, cpu_online_mask); */

/*    return ret; */
/* } */
/* EXPORT_SYMBOL(move_thread_to_node); */

/* struct task_struct * get_task_struct_from_pid(int pid) { */
/*    struct task_struct * task; */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       get_task_struct(task); */
/*    } */
/*    rcu_read_unlock(); */

/*    return task; */
/* } */
/* EXPORT_SYMBOL(get_task_struct_from_pid); */

/* int find_and_split_thp(int pid, unsigned long addr) { */
/*    struct task_struct *task; */
/*    struct mm_struct *mm = NULL; */
/*    struct vm_area_struct *vma; */
/*    struct page * page; */
/*    int ret = 1; */
/*    int err; */

/*    u64 start, stop; */
/*    rdtscll(start); */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       mm = get_task_mm(task); */
/*    } */
/*    rcu_read_unlock(); */
/*    if (!mm) { */
/*       ret = -ESRCH; */
/*       return ret; */
/*    } */

/*    down_read(&mm->mmap_sem); */

/*    vma = find_vma(mm, addr); */
/*    if (!vma || addr < vma->vm_start) { */
/*       ret = -EPAGENOTFOUND; */
/*       goto out_locked; */
/*    } */

/*    /\*if (!transparent_hugepage_enabled(vma)) { */
/*       ret = -EINVALIDPAGE; */
/*       goto out_locked; */
/*    }*\/ */
 
/*    page = follow_page(vma, addr, FOLL_GET); */

/*    err = PTR_ERR(page); */
/*    if (IS_ERR(page) || !page) { */
/*       ret = -EPAGENOTFOUND; */
/*       goto out_locked; */
/*    } */

/*    if(PageTransHuge(page)) { */
/*       // We found the page. Split it. */
/*       // split_huge_page does not create new pages. It will simple create new ptes and update the pmd */
/*       // see  __split_huge_page_map for details */
/*       ret = split_huge_page(page); */
/*    } */
/*    else { */
/*       ret = -EINVALIDPAGE; */
/*    } */

/*    put_page(page); */

/* out_locked: */
/*    //printk("[Core %d, PID %d] Releasing mm lock (0x%p)\n", smp_processor_id(), pid, &mm->mmap_sem); */
/*    up_read(&mm->mmap_sem); */
/*    mmput(mm); */

/*    rdtscll(stop); */
/*    carrefour_hook_stats.split_nb_calls++; */
/*    carrefour_hook_stats.time_spent_in_split += (stop - start); */

/*    return ret; */
/* } */
/* EXPORT_SYMBOL(find_and_split_thp); */

/* int find_and_migrate_thp(int pid, unsigned long addr, int to_node) { */
/*    struct task_struct *task; */
/*    struct mm_struct *mm = NULL; */
/*    struct vm_area_struct *vma; */
/*    struct page *page; */
/*    int ret = -EAGAIN; */
/*    int current_node; */

/*    pgd_t *pgd; */
/*    pud_t *pud; */
/*    pmd_t *pmd, orig_pmd; */

/*    rcu_read_lock(); */
/*    task = find_task_by_vpid(pid); */
/*    if(task) { */
/*       mm = get_task_mm(task); */
/*    } */
/*    rcu_read_unlock(); */
/*    if (!mm) { */
/*       ret = -ESRCH; */
/*       return ret; */
/*    } */

/*    down_read(&mm->mmap_sem); */

/*    vma = find_vma(mm, addr); */
/*    if (!vma || addr < vma->vm_start) { */
/*       ret = -EPAGENOTFOUND; */
/*       goto out_locked; */
/*    } */

/*    /\*if (!transparent_hugepage_enabled(vma)) { */
/*       ret = -EINVALIDPAGE; */
/*       goto out_locked; */
/*    }*\/ */
 
/*    pgd = pgd_offset(mm, addr); */
/*    if (!pgd_present(*pgd )) { */
/*       ret = -EINVALIDPAGE; */
/*       goto out_locked; */
/*    } */

/*    pud = pud_offset(pgd, addr); */
/*    if(!pud_present(*pud)) { */
/*       ret = -EINVALIDPAGE; */
/*       goto out_locked; */
/*    } */

/*    pmd = pmd_offset(pud, addr); */
/*    if (!pmd_present(*pmd ) || !pmd_trans_huge(*pmd)) { */
/*       ret = -EINVALIDPAGE; */
/*       goto out_locked; */
/*    } */

/*    // We found a valid pmd for this address and that's a THP */
/*    orig_pmd = *pmd; */

/*    // Mostly copied from the do_huge_pmd_numa_page function */
/* 	spin_lock(&mm->page_table_lock); */
/* 	if (unlikely(!pmd_same(orig_pmd, *pmd))) { */
/*       spin_unlock(&mm->page_table_lock); */
/* 		goto out_locked; */
/*    } */

/* 	page = pmd_page(*pmd); */
/* 	get_page(page); */

/* 	current_node = page_to_nid(page); */

/*    if(current_node == to_node) { */
/* 		put_page(page); */
/*       spin_unlock(&mm->page_table_lock); */
/*       ret = -ENOTMISPLACED; */
/* 		goto out_locked; */
/*    } */
/* 	spin_unlock(&mm->page_table_lock); */

/* 	/\* Acquire the page lock to serialise THP migrations *\/ */
/* 	lock_page(page); */

/* 	/\* Confirm the PTE did not while locked *\/ */
/* 	spin_lock(&mm->page_table_lock); */
/* 	if (unlikely(!pmd_same(orig_pmd, *pmd))) { */
/* 		unlock_page(page); */
/* 		put_page(page); */
/*       spin_unlock(&mm->page_table_lock); */
/* 		goto out_locked; */
/* 	} */

/*    if(carrefour_options.page_bouncing_fix_2M && (page->stats.nr_migrations >= carrefour_options.page_bouncing_fix_2M)) { */
/* 		unlock_page(page); */
/* 		put_page(page); */
/*       spin_unlock(&mm->page_table_lock); */
/*       ret = -EBOUNCINGFIX; */
/* 		goto out_locked; */
/*    } */


/*    page->dest_node = to_node; */

/*    // Make sure that pmd is tagged as "NUMA" */
/*    orig_pmd = pmd_mknuma(orig_pmd); */
/*    set_pmd_at(mm, addr & PMD_MASK, pmd, orig_pmd); */
   
/*    /\** FGAUD: We need to flush the TLB, don't we ? **\/ */
/*    flush_tlb_page(vma, addr); */

/* 	spin_unlock(&mm->page_table_lock); */

/*    if(carrefour_options.sync_thp_migration) { */
/*       /\* Migrate the THP to the requested node *\/ */
/*       ret = migrate_misplaced_transhuge_page(mm, vma, pmd, orig_pmd, addr, page, to_node); */

/*       if (ret > 0) { */
/*          //__DEBUG("Migrated THP 0x%lx successfully\n", addr); */
/*          ret = 0; */
/*       } */
/*       else { */
/*          //__DEBUG("Failed migrating THP 0x%lx (ret = %d)\n", addr, ret); */
/*          ret = -1; */
/*          // put page has been performed by migrate_misplaced_transhuge_page */

/*          // It failed */
/*          // We need to clear the pmd_numa_flag ...    */
/*          spin_lock(&mm->page_table_lock); */
/*          if (pmd_same(orig_pmd, *pmd)) { */
/*             orig_pmd = pmd_mknonnuma(orig_pmd); */
/*             set_pmd_at(mm, addr, pmd, orig_pmd); */
/*             VM_BUG_ON(pmd_numa(*pmd)); */
/*             update_mmu_cache_pmd(vma, addr, pmd); */
/*          } */
/*          spin_unlock(&mm->page_table_lock); */
/*       } */
/*    } */
/*    else { */
/*       ret = 0; */
/*       unlock_page(page); */
/*       put_page(page); */
/*    } */

/* out_locked: */
/*    //printk("[Core %d, PID %d] Releasing mm lock (0x%p)\n", smp_processor_id(), pid, &mm->mmap_sem); */
/*    up_read(&mm->mmap_sem); */
/*    mmput(mm); */

/*    return ret; */
/* } */
/* EXPORT_SYMBOL(find_and_migrate_thp); */

/* enum thp_states get_thp_state(void) { */
/* #ifdef CONFIG_TRANSPARENT_HUGEPAGE */
/*    if(test_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags)) { */
/*       return THP_ALWAYS; */
/*    } */
/*    else if(test_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags)) { */
/*       return THP_MADVISE; */
/*    } */
/*    else { */
/*       return THP_DISABLED; */
/*    } */
/* #else */
/*    return THP_DISABLED; */
/* #endif */
/* } */
/* EXPORT_SYMBOL(get_thp_state); */

/* void set_thp_state(enum thp_states state) { */
/* #ifdef CONFIG_TRANSPARENT_HUGEPAGE */
/*    if(state == THP_DISABLED) { */
/*       clear_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags); */
/*       clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags); */
/*    } */
/*    else if(state == THP_ALWAYS) { */
/*       set_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags); */
/*       clear_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags); */
/*    } */
/*    else if(state == THP_MADVISE){ */
/*       clear_bit(TRANSPARENT_HUGEPAGE_FLAG, &transparent_hugepage_flags); */
/*       set_bit(TRANSPARENT_HUGEPAGE_REQ_MADV_FLAG, &transparent_hugepage_flags); */
/*    } */
/*    else { */
/*       DEBUG_WARNING("Unknown state!\n"); */
/*    } */
/* #else */
/*    DEBUG_WARNING("Cannot change THP state since THP is not enabled in kernel options...\n"); */
/* #endif */
/* } */
/* EXPORT_SYMBOL(set_thp_state); */


/* unsigned migration_allowed_2M(void) { */
/*    unsigned t; */
/*    unsigned allowed; */
/*    struct carrefour_migration_stats_t* stats; */
   
/*    if(disable_2M_migrations_globally) */
/*       return 0; */

/*    if(!carrefour_options.throttle_2M_migrations_limit) */
/*       return 1; */

/*    read_lock(&carrefour_hook_stats_lock); */
/*    stats = get_cpu_ptr(&carrefour_migration_stats); */

/*    t = iteration_length_cycles ? (stats->time_spent_in_migration_2M * 100UL) / (iteration_length_cycles) : 0; */
   
/*    allowed = t < carrefour_options.throttle_2M_migrations_limit; */
/*    if(!allowed) { */
/*       __DEBUG("Thresold %u %% reached. Disabling 2M migrations\n", carrefour_options.throttle_2M_migrations_limit); */
/*       disable_2M_migrations_globally = 1; */
/*    } */

/*    put_cpu_ptr(&carrefour_migration_stats); */
/*    read_unlock(&carrefour_hook_stats_lock); */

/* /\*__DEBUG("THROTTLE: %llu %llu %u\n",  */
/*          (long long unsigned) carrefour_hook_stats.time_spent_in_migration_2M,  */
/*          (long long unsigned) iteration_length_cycles * num_online_cpus(),  */
/*          t);*\/ */

/*    return allowed; */
/* } */

/* unsigned migration_allowed_4k(void) { */
/*    unsigned t; */
/*    unsigned allowed; */
/*    struct carrefour_migration_stats_t* stats; */

/*    if(disable_4k_migrations_globally) */
/*       return 0; */

/*    if(!carrefour_options.throttle_4k_migrations_limit) */
/*       return 1; */

/*    read_lock(&carrefour_hook_stats_lock); */
/*    stats = get_cpu_ptr(&carrefour_migration_stats); */

/*    t = iteration_length_cycles ? (stats->time_spent_in_migration_4k * 100UL) / (iteration_length_cycles) : 0; */

/*    allowed = t < carrefour_options.throttle_4k_migrations_limit; */
/*    if(!allowed) { */
/*       __DEBUG("Thresold %u %% reached. Disabling 4k migrations\n", carrefour_options.throttle_4k_migrations_limit); */
/*       disable_4k_migrations_globally = 1; */
/*    } */

/*    put_cpu_ptr(&carrefour_migration_stats); */
/*    read_unlock(&carrefour_hook_stats_lock); */

/*    /\*__DEBUG("THROTTLE: %llu %llu %u\n",  */
/*          (long long unsigned) carrefour_hook_stats.time_spent_in_migration_4k[smp_processor_id()],  */
/*          (long long unsigned) iteration_length_cycles * num_online_cpus(),  */
/*          t);*\/ */

/*    return allowed; */
/* } */
