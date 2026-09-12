# STM32 clock manager service

The service aims for simplified clock management in more advanced use cases. Its basic features will be:

- turn on/turn off oscillators in a sound way, including timeouts
- discovery which oscillators are working during runtime (eg. sometimes HSE is not populated or not needed at all)
- use various timer features to measure oscillator frequency to be able to use arbitrary frequency crystals/oscillators
- automatically compute PLL configuration
- monitor oscillator failures in runtime and handle them
- monitor any oscillator deviations
- automatically configure bus prescalers
- allow changing clock frequency dynamically in runtime
- create a clock tree with dependencies and reference counting to automatically enable/disable bus and peripheral clocks when needed

The current state of the service is half-done PoC with a FSM to handle oscillator startup and a basic method of PLL configuration. Use at your own risk, YMMV.
