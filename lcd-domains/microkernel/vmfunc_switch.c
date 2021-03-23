#include <linux/mm.h>
#include <asm/desc.h>
#include <lcd_domains/types.h>
#include <asm/lcd_domains/libvmfunc.h>
#include <asm/lcd_domains/ept_lcd.h>
#include <asm/lcd_domains/bflank.h>
#include <asm/liblcd/address_spaces.h>
#include <libfipc.h>
#include <linux/kallsyms.h>
#include <asm/pgtable_64.h>
#ifdef CONFIG_LCD_TRACE_BUFFER
#include <linux/lcd_trace.h>
#endif
#ifdef CONFIG_LVD_PROTECT_FPU
#include <asm/fpu/internal.h>
#endif

#define NUM_LCDS		5
/* this is the only function Intel VT-x support */
#define VM_FUNCTION	0
#define NUM_ITERATIONS		10000000
#define CONFIG_VMFUNC_SWITCH_MICROBENCHMARK 1
#define REMAP_CR3_ALL_CPUS

/* exported by the microkernel. We trust that it's sane */
extern void *cpuid_page;
extern struct lcd *lcd_list[NUM_LCDS];
unsigned long init_pgd;

DECLARE_PER_CPU(unsigned long long, vmfunc_counter);

/* Linux kernel only provides ffs variant, which operates on 32-bit registers.
 * For promoting the bsf instruction to 64-bit, intel manual suggests to use
 * REX.W prefix to the instruction. However, when the operands are 64-bits, gcc
 * already promotes bsf to 64-bit.
 */
static __always_inline int ffsll(long long x)
{
	long long r;

	/*
	 * AMD64 says BSFL won't clobber the dest reg if x==0; Intel64 says the
	 * dest reg is undefined if x==0, but their CPU architect says its
	 * value is written to set it to the same as before, except that the
	 * top 32 bits will be cleared.
	 *
	 * We cannot do this on 32 bits because at the very least some
	 * 486 CPUs did not behave this way.
	 */
	asm("bsf %1,%0"
	    : "=r" (r)
	    : "rm" (x), "0" (-1));
	return r + 1;
}

int do_check(int ept)
{
	unsigned long vmfunc_load_addr;
	unsigned long vmfunc_sb_load_addr;
	int ret;
	static int once = 1;
	gpa_t gpa_vmfunc_lcd;
	gpa_t gpa_vmfunc_sb_lcd;
	hpa_t hpa_vmfunc;
	hpa_t hpa_vmfunc_sb;
	gpa_t gpa_lcd_stack;
	hpa_t hpa_lcd_stack;

	if (!once)
		return 0;
	vmfunc_load_addr = kallsyms_lookup_name("__vmfunc_trampoline_load_addr");
	vmfunc_sb_load_addr = kallsyms_lookup_name("__vmfunc_sboard_load_addr");
	gpa_vmfunc_lcd = isolated_lcd_gva2gpa(__gva(vmfunc_load_addr));
	gpa_vmfunc_sb_lcd = isolated_lcd_gva2gpa(__gva(vmfunc_sb_load_addr));
	gpa_lcd_stack = gpa_add(LCD_STACK_GP_ADDR, LCD_STACK_SIZE - sizeof(void*));

	ret = lcd_arch_ept_gpa_to_hpa_cpu(lcd_list[ept]->lcd_arch,
			gpa_vmfunc_lcd, &hpa_vmfunc, 1, smp_processor_id());
	if (ret) {
		printk("%s, Unable to find GPA to HPA mapping for vmfunc page! gva:%lx, gpa:%lx, ret = %d\n",
				__func__, vmfunc_load_addr, gpa_val(gpa_vmfunc_lcd), ret);
		return ret;
	}

	ret = lcd_arch_ept_gpa_to_hpa_cpu(lcd_list[ept]->lcd_arch,
			gpa_vmfunc_sb_lcd, &hpa_vmfunc_sb, 1, smp_processor_id());
	if (ret) {
		printk("%s, Unable to find GPA to HPA mapping for vmfunc page! gva:%lx, gpa:%lx, ret = %d\n",
				__func__, vmfunc_sb_load_addr, gpa_val(gpa_vmfunc_sb_lcd), ret);
		return ret;
	}

	printk("%s, Mapping found on LCDs EPT for trampoline GVA: %lx, GPA: %lx , HPA: %lx\n",
			__func__, vmfunc_load_addr, gpa_val(gpa_vmfunc_lcd),
			hpa_val(hpa_vmfunc));

	printk("%s, Mapping found on LCDs EPT for sboard GVA: %lx, GPA: %lx , HPA: %lx\n",
			__func__, vmfunc_sb_load_addr, gpa_val(gpa_vmfunc_sb_lcd),
			hpa_val(hpa_vmfunc_sb));


	if (once) {
		once = 0;
		print_hex_dump(KERN_DEBUG, ".vmfunc.trampoline.text.kernel: ", DUMP_PREFIX_ADDRESS,
			       16, 1, (void*) vmfunc_load_addr, 0x100, false);

		print_hex_dump(KERN_DEBUG, "vmfunc.trampoline.text.lcd: ", DUMP_PREFIX_ADDRESS,
			       16, 1, __va(hpa_val(hpa_vmfunc)), 0x100, false);

		print_hex_dump(KERN_DEBUG, ".vmfunc.sb.text.kernel: ", DUMP_PREFIX_ADDRESS,
			       16, 1, (void*) vmfunc_sb_load_addr, 0x100, false);

		print_hex_dump(KERN_DEBUG, "vmfunc.sb.text.lcd: ", DUMP_PREFIX_ADDRESS,
			       16, 1, __va(hpa_val(hpa_vmfunc_sb)), 0x100, false);

	}

	ret = lcd_arch_ept_gpa_to_hpa_cpu(lcd_list[ept]->lcd_arch,
			gpa_lcd_stack, &hpa_lcd_stack, 1, smp_processor_id());
	if (ret) {
		printk("%s, Unable to find GPA to HPA mapping for stack! gva:%lx, gpa:%lx, ret = %d\n",
				__func__, gva_val(isolated_lcd_gpa2gva(gpa_lcd_stack)), gpa_val(gpa_lcd_stack), ret);
		return ret;
	}

	printk("%s, Mapping found on LCDs EPT for stack GVA: %lx, GPA: %lx , HPA: %lx\n",
			__func__, gva_val(isolated_lcd_gpa2gva(gpa_lcd_stack)), gpa_val(gpa_lcd_stack),
			hpa_val(hpa_lcd_stack));

	return ret;
}

#ifndef CONFIG_DEFEAT_LAZY_TLB
static int vmfunc_prepare_switch(int ept)
{
	struct lcd *lcd = NULL;
	phys_addr_t cr3_base;
	gpa_t gpa_cr3;
	hpa_t hpa_lcd_cr3;

	asm volatile("mov %%cr3, %[cr3_base]"
			: [cr3_base]"=r"(cr3_base));

	/* gpa is the same as phys_addr */
	gpa_cr3 = __gpa(cr3_base);

	lcd = lcd_list[ept];
	current->vmfunc_lcd = lcd;

	if (lcd && !current->cr3_remapping) {
		hpa_lcd_cr3 = lcd->lcd_arch->hpa_cr3;

		LCD_MSG("[%d] %s: current->cr3 %llx", smp_processor_id(), current->comm, gpa_val(gpa_cr3));
		LCD_MSG("Add mapping inside LVDs EPT for gpa_cr3: %lx, hpa: %lx", gpa_val(gpa_cr3),
				hpa_val(hpa_lcd_cr3));

		lcd_arch_ept_map_all_cpus(lcd_list[ept]->lcd_arch, gpa_cr3,
			hpa_lcd_cr3,
			1, /* create, if not present */
			0 /* don't overwrite, if present */);

		current->cr3_remapping = 1;
		/* lcd_arch_ept_dump_this_cpu(lcd_list[ept]->lcd_arch); */
	}

	return 0;
}
#endif

void pick_stack(int ept)
{
	struct lcd *lcd = lcd_list[ept];
	int cpu = get_cpu();
	struct lcd_stack *this_stack = per_cpu_ptr(lcd->lcd_stacks, cpu);
	int bit = ffsll(this_stack->bitmap);

	if (bit && (bit <= NUM_STACKS_PER_CPU)) {
		bit -= 1;
		/* mark this bit as taken */
		this_stack->bitmap &= ~(1LL << bit);
		current->lcd_stack_bit = bit;
		current->lcd_stack = (void*) (gva_val(LCD_STACK_GV_ADDR)
					- (cpu * (NUM_STACKS_PER_CPU + 1) * LCD_STACK_SIZE)
					- (bit * LCD_STACK_SIZE)
					+ LCD_STACK_SIZE - sizeof(void*));
		/* record on which cpu allocated this stack */
		current->lcd_stack_cpu = cpu;
	} else {
		printk("Ran out of stacks on cpu %d, bitmap:%llx, ffsl ret=%d\n",
				cpu, this_stack->bitmap, bit);
	}
	if (this_stack->lazy_updated) {
		int bit = ffsll(this_stack->lazy_bitmap);
		unsigned long flags;
		printk("%s, clean lazy_stacks %llx\n", __func__, this_stack->lazy_bitmap);
		spin_lock_irqsave(&this_stack->lazy_bm_lock, flags);

		/* process all the lazy bits */
		do {
			if (bit && (bit <= NUM_STACKS_PER_CPU)) {
				/* mark this bit as free in the original bitmap */
				this_stack->bitmap |= (1LL << bit);
				/* mark it as zero */
				this_stack->lazy_bitmap &= ~(1LL << (bit-1));
			}
		} while ((bit = ffsll(this_stack->lazy_bitmap)));

		/* update variable to false */
		this_stack->lazy_updated = false;
		spin_unlock_irqrestore(&this_stack->lazy_bm_lock, flags);
	}
	put_cpu();
	BUG_ON(!current->lcd_stack);
}

void drop_stack(int ept)
{
	struct lcd *lcd = lcd_list[ept];

	/*
	 * Perform normal deallocation if we are on the same cpu as the
	 * allocated one
	 */
	if (smp_processor_id() == current->lcd_stack_cpu) {
		struct lcd_stack *this_stack = per_cpu_ptr(lcd->lcd_stacks, get_cpu());

		current->lcd_stack = NULL;
		this_stack->bitmap |= (1LL << current->lcd_stack_bit);
		put_cpu();
	} else {
		/*
		 * perform lazy deallocation
		 */
		unsigned long flags;
		struct lcd_stack *other_stack = per_cpu_ptr(lcd->lcd_stacks, current->lcd_stack_cpu);
		spin_lock_irqsave(&other_stack->lazy_bm_lock, flags);
		other_stack->lazy_bitmap |= (1LL << current->lcd_stack_bit);
		other_stack->lazy_updated = true;
		spin_unlock_irqrestore(&other_stack->lazy_bm_lock, flags);
	}
}

#ifdef CONFIG_LVD_PROTECT_FPU
void save_fpu_regs(void)
{
	/*
	 * Check if kernel is using the FPU
	 */
	if (kernel_fpu_disabled()) {
		//printk("%s: [%s:%d] kernel fpu state\n", __func__, current->comm, current->pid);
		/* Save if kernel is using the FPU */
		copy_fpregs_to_fpstate(&current->kernel_fpu);
	} else {
		struct fpu *fpu = &current->thread.fpu;
		/* if not, save it only if fpregs_active is set */
		if (fpu->fpregs_active) {
			//printk("%s: [%s:%d] user fpu state\n", __func__, current->comm, current->pid);
			copy_fpregs_to_fpstate(fpu);
		}
	}
}

void restore_fpu_regs(void)
{
	/*
	 * Check if kernel was using the FPU
	 */
	if (kernel_fpu_disabled()) {
		//printk("%s: [%s:%d] kernel fpu state\n", __func__, current->comm, current->pid);
		/* Restore if kernel was using the FPU */
		copy_kernel_to_fpregs(&current->kernel_fpu.state);
	} else {
		struct fpu *fpu = &current->thread.fpu;
		/* if user was using it, restore it only if fpregs_active is set */
		if (fpu->fpregs_active) {
			//printk("%s: [%s:%d] user fpu state\n", __func__, current->comm, current->pid);
			copy_kernel_to_fpregs(&fpu->state);
		}
	}

}
#endif

int vmfunc_klcd_wrapper(struct fipc_message *msg, unsigned int ept)
{
	int ret = 0;
	unsigned long flags;

	if (ept > 511) {
		ret = -EINVAL;
		goto exit;
	}
#ifndef CONFIG_DEFEAT_LAZY_TLB
	vmfunc_prepare_switch(ept);
	ret = do_check(ept);
	if (ret)
		goto exit;
#endif
	if (!init_pgd)
		init_pgd = kallsyms_lookup_name("init_level4_pgt");

	local_irq_save(flags);

	if (current->nested_count++ == 0)
		pick_stack(ept);

#ifdef CONFIG_LVD_PROTECT_FPU
	save_fpu_regs();
#endif

	local_irq_restore(flags);
#if 0
	printk("%s [%d]: entering on cpu %d, rpc_id: %x | lcd_stack %p\n",
			current->comm,
			current->pid,
			smp_processor_id(),
			msg->rpc_id,
			current->lcd_stack);
#endif
#ifdef CONFIG_LCD_TRACE_BUFFER
	add_trace_entry(EVENT_VMFUNC_TRAMP_ENTRY, msg->rpc_id);
#endif

#ifdef CONFIG_LCD_VMFUNC_COUNTERS
	per_cpu(vmfunc_counter, smp_processor_id())++;
#endif

	vmfunc_trampoline_entry(msg);

#ifdef CONFIG_LCD_TRACE_BUFFER
	add_trace_entry(EVENT_VMFUNC_TRAMP_EXIT, msg->rpc_id);
#endif

	local_irq_save(flags);
	if (--current->nested_count == 0)
		drop_stack(ept);

	local_irq_restore(flags);

exit:
	return ret;
}

int vmfunc_klcd_test_wrapper(struct fipc_message *msg, unsigned int ept, vmfunc_test_t test)
{
	int ret = 0;
	unsigned long flags;

	if (ept > 511) {
		ret = -EINVAL;
		goto exit;
	}
	/* FIXME: This should be done only once per process.
	 * create a TLS variable to mark if cr3 switch is already active.
	 */
#ifndef CONFIG_DEFEAT_LAZY_TLB
	vmfunc_prepare_switch(ept);
	ret = do_check(ept);
	if (ret)
		goto exit;

	printk("%s [%d]: Starting VMFUNC tests\n",
			__func__, smp_processor_id());
#endif
	if (!init_pgd)
		init_pgd = kallsyms_lookup_name("init_level4_pgt");

	local_irq_save(flags);
	if (current->nested_count++ == 0)
		pick_stack(ept);
	local_irq_restore(flags);

	ret = vmfunc_test_wrapper(msg, test);

	local_irq_save(flags);
	if (--current->nested_count == 0)
		drop_stack(ept);
	local_irq_restore(flags);

exit:
	return ret;
}

#ifdef CONFIG_DEFEAT_LAZY_TLB
int noinline
remap_cr3(void)
{
	struct lcd *lcd = NULL;
	phys_addr_t cr3_base;
	gpa_t gpa_cr3;
	hpa_t hpa_lcd_cr3 = {0};
	/* we always switch to eptp_list[1] */
	int ept = 1;
	int ret = 0;

	//if (current->mapped_cr3 != __pa(current->active_mm->pgd))
	//	printk(KERN_EMERG "%s, remap gpa:%lx , hpa %lx\n",
	//		__func__, __pa(current->active_mm->pgd),
	//		hpa_val(lcd_list[ept]->lcd_arch->hpa_cr3));

	lcd = lcd_list[ept];
	current->vmfunc_lcd = lcd;
	if (lcd)
		hpa_lcd_cr3 = lcd->lcd_arch->hpa_cr3;

	/* map init_level4_pgt */
	if (lcd && !lcd->lcd_arch->idle_cr3_mapped) {

		if (!init_pgd)
			goto skip;

		gpa_cr3 = __gpa(__pa(init_pgd));
#ifdef REMAP_CR3_ALL_CPUS
		ret = lcd_arch_ept_map_all_cpus(lcd_list[ept]->lcd_arch, gpa_cr3,
			hpa_lcd_cr3,
			1, /* create, if not present */
			1 /* overwrite, if present */);
#else
		ret = lcd_arch_ept_map_cpu(lcd_list[ept]->lcd_arch, gpa_cr3,
			hpa_lcd_cr3,
			1, /* create, if not present */
			1, /* overwrite, if present */
			smp_processor_id());

#endif

		lcd->lcd_arch->idle_cr3_mapped = 1;

		if (ret)
			goto fail;
	}

skip:
	cr3_base = __pa(current->active_mm->pgd);
	/* gpa is the same as phys_addr */
	gpa_cr3 = __gpa(cr3_base);

	/*
	 * For some weird reason, processes with pid 0 occasionally fails to
	 * find the vmfunc page even if mapped before. So, try to always map
	 * those processes, even if mapped.
	 */
	if (lcd && !current->pid)
		goto force_map;

	/*
	 * XXX: We overwrite even if the entry is present. why?  Since the
	 * Linux kernel uses lazy cr3 switch, we may run into a situation where
	 * the application has entered this wrapper with cr3_a and later
	 * through cr3_b due to a context switch. Since, we save only the last
	 * save cr3 in mapped_cr3 varible, we won't even know if we have mapped
	 * cr3_a if the application decides to enter with cr3_a.
	 * t0 ........  t1 ........ t2
	 * cr3_a -----> cr3_b ----> cr3_a
	 * So, simply overwrite this mapping as it would not harm us.
	 */
	if (lcd && (current->mapped_cr3 != cr3_base)) {
force_map:
		ret = lcd_arch_ept_map_all_cpus(lcd_list[ept]->lcd_arch, gpa_cr3,
			hpa_lcd_cr3,
			1, /* create, if not present */
			1 /* overwrite, if present */);

		current->mapped_cr3 = cr3_base;
		if (ret)
			goto fail;
	}

	return 0;
fail:
	LCD_MSG("%s[%d] Remapping of cr3 failed for gpa: %llx, hpa: %llx",
			current->comm, current->pid,
			gpa_val(gpa_cr3), hpa_val(hpa_lcd_cr3));
	return ret;
}
#endif

int noinline
vmfunc_test_wrapper(struct fipc_message *request, vmfunc_test_t test)
{
#ifdef CONFIG_DO_BF_PAGE_WALK
	unsigned long vmfunc_load_addr =
		kallsyms_lookup_name("__vmfunc_trampoline_load_addr");
	gva_t gva_lcd_stack = gva_add(LCD_STACK_GV_ADDR, LCD_STACK_SIZE - sizeof(void*));
	unsigned long idtr_base;
	unsigned char idt_ptr[10];

	asm volatile("sidt %[idt_ptr]"
			:[idt_ptr]"=m"(idt_ptr));

	idtr_base = *(unsigned long *)&idt_ptr[2];
#endif
	switch (test) {
	case VMFUNC_TEST_EMPTY_SWITCH:
		printk("%s: Invoking EMPTY_SWITCH test\n", __func__);
#ifdef CONFIG_DEFEAT_LAZY_TLB
		remap_cr3();
#endif
		__vmfunc_call_empty_switch();
		printk("Done!");
		vmfunc_call_empty_switch(0);
#ifdef CONFIG_DO_BF_PAGE_WALK
		bfcall_guest_page_walk(vmfunc_load_addr, __pa(current->active_mm->pgd), 1);
		bfcall_guest_page_walk(gva_val(gva_lcd_stack), __pa(current->active_mm->pgd), 1);
		bfcall_guest_page_walk(idtr_base, __pa(current->active_mm->pgd), 1);
		bfcall_guest_page_walk(0xffff8827df00b000UL, __pa(current->active_mm->pgd), 1);
#endif

#ifdef CONFIG_VMFUNC_SWITCH_MICROBENCHMARK

		{
			int i = 0;
			u64 start = rdtsc(), end;
			for (; i < NUM_ITERATIONS; i++) {
				__vmfunc_call_empty_switch();
			}
			end = rdtsc();
			printk("%d iterations of vmfunc (same domain) back-to-back "
					"took %llu cycles (avg: %llu cycles)\n",
					NUM_ITERATIONS,
					end - start,
					(end - start) / NUM_ITERATIONS);
		}


		{
			int j = 0;
			for (; j < 5; j++) {
				int i = 0;
				u64 start = rdtsc(), end;
				for (; i < NUM_ITERATIONS; i++) {
					int k = 0;
					for (; k < j; k++)
						asm volatile("add %eax, %eax\n\t");
					vmfunc_call_empty_switch(0);
				}
				end = rdtsc();
				cond_resched();
				printk("%d iterations of vmfunc back-to-back with %d add "
						"insns took %llu cycles (avg: %llu cycles)\n",
						NUM_ITERATIONS, j,
						end - start,
						(end - start) / NUM_ITERATIONS);
			}
		}
#endif	/* CONFIG_VMFUNC_SWITCH_MICROBENCHMARK */
		break;
	case VMFUNC_TEST_DUMMY_CALL:
		/* only upto vmfunc_id 0x3 is handled */
		request->vmfunc_id = 0x4;
		printk("%s: Invoking DUMMY_CALL test with vmfunc_id: %d\n",
					__func__, request->vmfunc_id);
		vmfunc_trampoline_entry(request);
		break;
	case VMFUNC_TEST_RPC_CALL_BENCHMARK:
		request->vmfunc_id = VMFUNC_RPC_CALL;
		{
			int i = 0;
			u64 start = rdtsc(), end;
			for (; i < NUM_ITERATIONS; i++) {
				vmfunc_trampoline_entry(request);
			}
			end = rdtsc();
			printk("%d iterations of rpc_call took %llu cycles (avg: %llu cycles)\n",
					NUM_ITERATIONS, end - start, (end - start) / NUM_ITERATIONS);
		}
		break;

	case VMFUNC_TEST_RPC_CALL:
		request->vmfunc_id = VMFUNC_RPC_CALL;
		vmfunc_trampoline_entry(request);
		break;
	case VMFUNC_TEST_RPC_CALLBACK:
		request->vmfunc_id = VMFUNC_RPC_CALL;
		vmfunc_trampoline_entry(request);
		break;

	}
	return 0;
}

EXPORT_SYMBOL(vmfunc_klcd_test_wrapper);
EXPORT_SYMBOL(vmfunc_klcd_wrapper);
EXPORT_SYMBOL(vmfunc_test_wrapper);
