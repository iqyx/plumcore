# nwdaq-inc5 single-axis inclination sensing module

## Overview

`nwdaq-inc5` is a plumCore port for the single-axis inclination sensing module. The firmware brings up the board, runs the analog front-end that measures the inclination (tilt) of a liquid sensor together with the PCB temperature, and exposes both quantities as `Sensor` interfaces that an application can read. A serial console is provided for logging.

## Target hardware

|               |                                             |
|---------------|---------------------------------------------|
| MCU           | STM32G491CCU6 (ARM Cortex-M4F)              |
| Platform      | `cortex-m4f`                                |
| MCU family    | `stm32g4`                                   |
| Core clock    | 16 MHz (HSI, `SystemCoreClock = 16e6`)      |
| Flash         | 256 KiB starting at `0x08000000`            |
| RAM           | 80 KiB at `0x20000000` (top 2 KiB reserved) |
| FreeRTOS heap | 60 KiB (newlib malloc wrapper)              |

The port is built entirely against CMSIS register definitions (`<stm32g4xx.h>`); peripheral access uses `RCC->`, `NVIC_*`, `SysTick->`, `TIMx->` and `DAC1->` directly together with the plumCore GPIO/SPI/UART driver services. libopencm3 is only linked for the startup/vector table, so the interrupt handlers keep their libopencm3 vector names (`usart1_isr`, `tim4_isr`, `tim2_isr`).

## What the firmware does

### Clocks and GPIO

`port_early_init()` enables the peripheral clocks (GPIOA/B/C, USART1, DAC1, SPI1/SPI3, TIM6, ADC12) and selects SYSCLK as the ADC kernel clock. The three GPIO ports are wrapped by `stm32-gpio` driver instances (`gpioa`, `gpiob`, `gpioc`) and all pin configuration goes through the GPIO interface.

### Serial console

USART1 is configured on **PB6/PB7** (AF7) at **115200 baud**. The resulting stream is advertised to the service locator as `console` and registered as the default log output. RX data is handled in `usart1_isr`.

### Liquid-sensor excitation

The liquid tilt sensor is driven with an AC excitation:

- **DAC1** outputs two static reference levels — channel 1 at 0.35 and channel 2 at 0.65 of full scale (12-bit) — on **PA4/PA5**.
- **TIM4** (1 MHz time base, 2.5 ms period) drives the measurement sequence. Its compare channels CC3 and CC4 generate interrupts that mark the *measure* and *data-ready* instants within each cycle, and the update event advances the cycle counter. On every cycle the MUX-select output **PA2** is toggled, alternating the excitation polarity.

### Inclination / temperature acquisition

An external **MCP3564** 24-bit delta-sigma ADC is read over **SPI1** (**PA6/PA7** + **PB3**, AF5; chip-select on **PC4**) at 4 MHz, gain 1, OSR 256, in IRQ mode. The `adc-task` FreeRTOS task, synchronised to the TIM4 events, sequences the converter:

1.  Select the input MUX (alternating between the inclination channel CH0 and the temperature channel CH1).
2.  Start a conversion at the timed *measure* instant and read the result at *data-ready*.
3.  Perform synchronous detection: the sample is added or subtracted depending on the excitation polarity of the current half-cycle.
4.  Every 80 steps the accumulators are averaged and published.

The averaged results are converted to physical quantities — the inclination is scaled to LSB, and the temperature is derived from an NTC (β = 3977, 10 kΩ reference) and expressed in °C.

### Exposed sensors

Two `Sensor` interfaces are registered with the service locator:

| Name     | Description             | Unit |
|----------|-------------------------|------|
| `inc_x`  | single-axis inclination | LSB  |
| `temp_x` | PCB temperature         | °C   |

Each sensor has its own "value ready" semaphore, so both can be read independently at the full measurement rate. A reader blocks in the sensor's `value_f` call until the next measurement becomes available.

### Other

- **PB12/PB13** drive status LEDs; PB12 is toggled once per output cycle.
- **TIM6** runs as a 1 MHz free-running counter that provides the high-resolution time base for FreeRTOS run-time task statistics (`port_task_timer_init` / `port_task_timer_get_value`).

## Building

```shell
defconfig config/nwdaq_inc5_defconfig
scons
```

A separate bootloader configuration is available as `config/nwdaq_inc5_bl_defconfig`.
