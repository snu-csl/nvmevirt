#include "nvmev.h"

void __cyg_profile_func_enter(void *this_fn, void *call_site) __attribute__((no_instrument_function));
void __cyg_profile_func_exit(void *this_fn, void *call_site) __attribute__((no_instrument_function));

int lv = 0;

void __cyg_profile_func_enter(void *this_fn, void *call_site) {
	lv++;
	char buffer[32];
	for (int i = 0; i < lv; i++)
		strcat(buffer, " ");
	printk(KERN_WARNING "%s: %s +enter{func: '%ps'}", NVMEV_DRV_NAME, buffer, this_fn);
}

void __cyg_profile_func_exit(void *this_fn, void *call_site) {
	char buffer[32];
	for (int i = 0; i < lv; i++)
		strcat(buffer, " ");
	printk(KERN_WARNING "%s: %s -exit{func: '%ps'}", NVMEV_DRV_NAME, buffer, this_fn);
	lv--;
}
