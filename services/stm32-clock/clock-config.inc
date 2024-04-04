#if defined(STM32G4)
const struct stm32_clock_conf clock_configs[] = {
	{
		.sysclk = 16e6,
		.vcore_range = 2,
		.vcore_boost = false,
		.flash_wait_states = 1,
	}, {
		.sysclk = 64e6,
		.vcore_range = 1,
		.vcore_boost = false,
		.flash_wait_states = 2,
	}, {
		.sysclk = 128e6,
		.vcore_range = 1,
		.vcore_boost = false,
		.flash_wait_states = 4,
	}, {
		.sysclk = 170e6,
		.vcore_range = 1,
		.vcore_boost = true,
		.flash_wait_states = 4,
	}, {
		.sysclk = 0,
	}
};
#else
	#error "No clock configuration available for the selected MCU"
#endif
