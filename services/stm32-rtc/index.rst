=======================================
STM32 RTC peripheral clock driver
=======================================


To-Do
---------------------------------------

- the service provides an old-style IClock interface. Update to the new style. Update all depending services
- extend the provided interface:
  - allow direct counter readout
  - clock step-adjust (set)
  - clock coarse adjust (per second adjustment)
  - fine adjust (clock pulse skip/insert)
  - thread safety
- sync out capability (RTC_OUT)
- 
