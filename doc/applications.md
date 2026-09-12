# Applications

An application is a high‑level service that implements the device’s business logic while reusing the plumCore framework services and the board‑specific port. Applications behave exactly like services (same lifecycle, interfaces), but contain the orchestration and policies that make a device perform a particular function.

Applications are responsible mainly for:

- composing and configuring child services (data processing, storage, networking, UI) and managing their lifecycle
- routing data between services as required by the end product
- defining telemetry endpoints
- implementing product-specific user‑facing behavior — for example button handling, LED feedback, additional CLI structure, or web UI actions

Because an application conforms to the service structure, it can be swapped independently of the port: the same application can run on multiple ports if required services/interfaces and abstractions exist, and the same port can host different applications, enabling reuse across device classes like DAQ systems, remote GPIO, gateways, etc.

An application directory contains the following items:

- source code implementing application startup and cleanup
- Kconfig file to define application compile-time configuration
- SConscript file with build rules
- tests, if present
- documentation (doxygen and reST)

An example application could be eg. a HMI application, which:

- can run on many different devices using different ports, if they advertise a display device (a LCD, a TFT screen) and some sort of an input device (buttons, touchscreen)
- provides a generic protocol to draw on the output device and to get the user input
- makes these protocols accessible over ethernet and many other custom buses
- implements some sort of screen sleep function
- implements touchscreen calibration if required
- reads an accelerometer and optionally rotates the framebuffer device to always display content in the corrent orientation
