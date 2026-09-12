# Crash manager service

Crash manager service is the one responsible for catching and resolving unusual system states. Since bugs in software are inevitable, the whole embedded system may experience a "run-away", mainly if it is not memory protected. Even when MPU is used, we need a service dedicated for handling all kinds of exceptions. Main goals of the crash-mgr service are:

- install applicable exception vectors and call a generic exception handler when they occur
- if allowed, preallocate an unused memory space for creating system dumps
- make a system dump in the generic exception handler according to the configuration (registers, tasks/threads, memory, logs, etc.), compress it, encrypt/sign it, save it
- notify the user (emit a log message, blink a LED)
- notify the upstream system (emit a log message, set some status variables, post a message to a message queue)

## to-do

The following functionality is not well suited for the crash-mgr to handle. It will be implemented in a dedicated service:

- check if the bootup sequence (boot reason, boot time, boot loop) is within limits, reboot or invoke the bootloader if not
- catch resource starvation (CPU, memory)
- protect itself from common types of system faults, try to recover
- configure hardware watchdog and catch pre-reset exceptions, log them, call a generic exception handler
