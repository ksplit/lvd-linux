/*
 * create.c - Basic code for creating an LCD.
 *
 * Copyright: University of Utah
 */

#include <libcap.h>
#include <liblcd/liblcd.h>
#include <lcd_domains/microkernel.h>


extern struct task_struct *klcd_thread;

int lvd_create(cptr_t *lcd, int lvd_id)
{
	cptr_t slot;
	int ret;
	/*
	 * Alloc slot for new object
	 */
	ret = lcd_cptr_alloc(&slot);
	if (ret) {
		LIBLCD_ERR("cptr alloc failed");
		goto fail1;
	}
	/*
	 * Make LCD
	 */
	ret = __lvd_create(current->lcd, slot, lvd_id);
	if (ret) {
		LIBLCD_ERR("lcd create failed");
		goto fail2;
	}

	*lcd = slot;

	return 0;

fail2:
	lcd_cptr_free(slot);
fail1:
	return ret;
}

int lcd_create(cptr_t *lcd)
{
	cptr_t slot;
	int ret;
	/*
	 * Alloc slot for new object
	 */
	ret = lcd_cptr_alloc(&slot);
	if (ret) {
		LIBLCD_ERR("cptr alloc failed");
		goto fail1;
	}
	/*
	 * Make LCD
	 */
	ret = __lcd_create(current->lcd, slot);
	if (ret) {
		LIBLCD_ERR("lcd create failed");
		goto fail2;
	}

	*lcd = slot;

	return 0;

fail2:
	lcd_cptr_free(slot);
fail1:
	return ret;
}

int lcd_create_klcd(cptr_t *klcd)
{
	cptr_t slot;
	int ret;
	/*
	 * Alloc slot for new object
	 */
	ret = lcd_cptr_alloc(&slot);
	if (ret) {
		LIBLCD_ERR("cptr alloc failed");
		goto fail1;
	}
	/*
	 * Make kLCD
	 */
	ret = __lcd_create_klcd(current->lcd, slot);
	if (ret) {
		LIBLCD_ERR("klcd create failed");
		goto fail2;
	}

	*klcd = slot;

	return 0;

fail2:
	lcd_cptr_free(slot);
fail1:
	return ret;
}

int lvd_create_klcd(cptr_t *klcd)
{
	cptr_t slot;
	int ret;
	/*
	 * Alloc slot for new object
	 */
	ret = lcd_cptr_alloc(&slot);
	if (ret) {
		LIBLCD_ERR("cptr alloc failed");
		goto fail1;
	}
	/*
	 * Make kLCD
	 */
	ret = __lvd_create_klcd(current->lcd, slot);
	if (ret) {
		LIBLCD_ERR("klcd create failed");
		goto fail2;
	}

	*klcd = slot;

	return 0;

fail2:
	lcd_cptr_free(slot);
fail1:
	return ret;
}

int lcd_config_registers(cptr_t lcd, gva_t pc, gva_t sp, gpa_t gva_root,
			gpa_t utcb_page)
{
	return __lcd_config(current->lcd, lcd, pc, sp, gva_root, utcb_page);
}

int lcd_save_cr3(cptr_t lcd, void *lcd_ptables)
{

	return __lcd_save_cr3(current->lcd, lcd, __hpa(__pa(lcd_ptables)));
}

int lcd_memory_grant_and_map(cptr_t lcd, cptr_t mo, cptr_t dest_slot,
			gpa_t base)
{
	return __lcd_memory_grant_and_map(klcd_thread->lcd, lcd, mo,
					dest_slot, base);
}

int lcd_memory_grant_and_map_hpa(cptr_t lcd, cptr_t mo, cptr_t dest_slot,
			gpa_t base, hpa_t hpa_base)
{
	return __lcd_memory_grant_and_map_hpa(klcd_thread->lcd, lcd, mo,
					dest_slot, base, hpa_base);
}

int lcd_memory_grant_and_map_cpu(cptr_t lcd, cptr_t mo, cptr_t dest_slot,
			gpa_t base, int cpu)
{
	return __lcd_memory_grant_and_map_cpu(klcd_thread->lcd, lcd, mo,
					dest_slot, base, cpu);
}

int lcd_memory_grant_and_map_percpu(cptr_t lcd, cptr_t mo, cptr_t dest_slot,
			gpa_t base, int cpu)
{
	return __lcd_memory_grant_and_map_percpu(klcd_thread->lcd, lcd, mo,
					dest_slot, base, cpu);
}

int lcd_cap_grant(cptr_t lcd, cptr_t src, cptr_t dest)
{
	return __lcd_cap_grant(klcd_thread->lcd, lcd, src, dest);
}

int lcd_set_struct_module_hva(cptr_t lcd, struct module *mod)
{
	return __lcd_set_struct_module_hva(current->lcd, lcd, mod);
}

int lcd_run(cptr_t lcd)
{
	return __lcd_run(current->lcd, lcd);
}

int lcd_stop(cptr_t lcd)
{
	return __lcd_stop(current->lcd, lcd);
}

/* EXPORTS -------------------------------------------------- */

EXPORT_SYMBOL(lvd_create);
EXPORT_SYMBOL(lcd_create);
EXPORT_SYMBOL(lcd_create_klcd);
EXPORT_SYMBOL(lvd_create_klcd);
EXPORT_SYMBOL(lcd_config_registers);
EXPORT_SYMBOL(lcd_memory_grant_and_map);
EXPORT_SYMBOL(lcd_memory_grant_and_map_hpa);
EXPORT_SYMBOL(lcd_memory_grant_and_map_cpu);
EXPORT_SYMBOL(lcd_cap_grant);
EXPORT_SYMBOL(lcd_run);
EXPORT_SYMBOL(lcd_stop);
EXPORT_SYMBOL(lcd_set_struct_module_hva);
