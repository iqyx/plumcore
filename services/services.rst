.. _services:

=========================
Services
=========================

In plumCore, being a microkernel architecture, most of the functionality is implemented as unprivileged
user-space services. A service generally consists of a state (its assigned memory), threads with their
corresponding stacks, one or more interfaces the service provides and a bunch of depenencies on interfaces
provided by other services or the microkernel itself.

There are multiple groups of services for different purposes, albeit not strictly marked or split in any way.

Low level architecture-dependent services provide means of using or controlling systick or interrupts,
SoC-dependent services allow accessing SoC's peripherals and communication interfaces, port-dependent services
(although rare) may utilize some specific port information to control part of its hardware.

Higher level services never access hardware or SoC resources directly. They usually depend on interfaces
provided by low level services. In the result they allow access to memories, filesystems, GNSS recivers,
radio interfaces, etc.

There are even higher level services possible providing command line interfaces, user interfaces using
displays, high level encrypted network connectivity, data-driven flow graph processing capability or any
other functionality abstract enough to not depend on any low level specifics.


Low level device drivers
=============================

.. toctree::

	stm32-adc/index
	stm32-dac/index
	stm32-fdcan/index
	stm32-i2c/index
	stm32-l4-pm/index
	stm32-qspi-flash/index
	stm32-rtc/index
	stm32-spi/index
	stm32-system-clock/index
	stm32-timer-capsense/index
	stm32-uart/index
	stm32-watchdog/index



High level (second level) device drivers
=============================================

.. toctree::

	adc-sensor/adc-sensor
	bq35100/bq35100
	gps-ublox/index
	gsm-quectel/gsm-quectel
	icm42688p/icm42688p
	adxl355/index
	si7006/index
	



DAQ and data processing services
======================================

.. toctree::

	data-process/data-process
	mq-batch/index
	mq-lora-bridge/index
	mq-periodogram/index
	mq-sensor-source/index
	mq-stats/index
	mq-ws-source/index



Volume and filesystem access
======================================

.. toctree::

	fs-spiffs/fs-spiffs
	flash-cbor-mib/index
	flash-fifo/index
	flash-nvm/index
	flash-test/index
	flash-vol-static/index
	lora-fs/index	


Communication protocols
============================

.. toctree::

	mqtt-tcpip/mqtt-tcpip
	nbus/nbus


Communication server/clients
=================================

.. toctree::

	mqtt-file-download/mqtt-file-download
	mqtt-file-server/mqtt-file-server
	mqtt-sensor-upload/mqtt-sensor-upload


User interface services
============================

.. toctree::

	loginmgr/loginmgr
	cli/cli


Generic system services
==================================

.. toctree::

	plocator/plocator

   
