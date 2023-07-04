==================================
Applets
==================================

Introduction
========================

Over time it became clear that some use cases may benefit from using a small helper programs to perform simple
housekeeping tasks, usually run by the user. Plumcore was challenged to run them occasionally, on demand, without
introducing much burden to the running system. The obvious place to run them was directly in the CLI service. Which
means:

- it is hard to build those helpers selectively, eg. depending on the service they are helping
- the CLI service started to have unmaintainable amount of dependencies
- it is simply a very flawed concept

An ``Applet`` interface was defined describing what is the helper program doing (now called an *applet*), how to use
it, how to run it and how to communicate with it when it is running.


What are *applets*
==========================

Applet is a small helper program, generally performing a simple task to help the user of the system. They directly
interact with the user by means of a standard input and output. Note that there is no standard error output. Instead,
an applet gets a reference to a ``Logger`` instance which can be used to log events and errors.

Applets are run in the context of the parent service. May it be the CLI service (running the applet with a command)
or any other service running in the system and providing means of user interaction (eg. a network server).

They are usually compiled together with the main firmware and saved in the device flash memory. In the interpreted
form, they are included in the firmware either as a bytecode or as a source code and run in runtime using an interpreter
service (ecmascript, Wren, etc.)

The ``Applet`` interface contains:

- name of the applet as a string
- human readable help formatted as restructured text
- entry point of the applet (a compiled function in case of compiled applets, a function name for interpreted applets)
- reference to the stdin/stdout ``Stream``, which the parent service is properly handling
- reference to the ``Logger`` interface
- and a flag whether the applet wants to be run in a dedicated thread (together with the required stack size)

It should be noted that, depending on the applet code requirements, it may not be possible to run the particular applet
because of lack of resources. Applets are not considered essential parts of the system and they are treated as such.
No memory is preallocated during the initialisation phase of the system as is mandated for essential services.


Tutorial: create your own applet
=======================================

Applets are residing in their own subdirectories in the ``applets/`` top level directory. The applet directory
contains:

- ``SConscript`` file defining source code of the applet which should be compiled
- ``KConfig`` file with a configuration directive allowing the user to select if the particular applet should be
  included in the final firmware
- a main applet header
- applet source code


Decide on the applet name. We will call it ``TEMPCO_CALIBRATION``. First create the applet directory
``tempco-calibration`` with a ``SConscript`` file inside. Scons will check if a configuration variable
``APPLET_TEMPCO_CALIBRATION`` is set and compile all C source files in the current directory if yes.
It is a recommended practice to name applet configuration variables starting with ``APPLET_``.

.. code-block:: python
	:linenos:

	Import("env")
	Import("objs")
	Import("conf")

	if conf["APPLET_TEMPCO_CALIBRATION"] == "y":
		objs.append(env.Object(File(Glob("*.c"))))

Continue with creating a ``KConfig`` file with a configuration tree fragment. It should contain a single ``bool``
configuration option enabling the applet compilation. You may add any other config options you want too, it is a good
practice to hide them in a dedicated menu visible only if the applet is enabled.

.. code-block:: kconfig
	:linenos:

	config APPLET_TEMPCO_CALIBRATION
		bool "Output adc-composite temperature coeff calibration data as a CSV"

	menu "tempco-calibration configuration"
		depends on APPLET_TEMPCO_CALIBRATION

	endmenu


Add a basic main header file ``tempco-cal.h``. There is nothing specific inside:

.. code-block:: c
	:linenos:

	#pragma once

	#include <interfaces/applet.h>

	extern const Applet tempco_calibration;

Add some C source to a ``tempco-cal.c`` file:

.. code-block:: c
	:linenos:

	#include <stdint.h>
	#include <main.h>
	#include <interfaces/applet.h>

	#include "tempco-cal.h"

	static applet_ret_t tempco_calibration_main(Applet *self, struct applet_args *args) {

		/* Applet code goes here */

		return APPLET_RET_OK;
	}

	const Applet tempco_calibration = {
		.executable.compiled = {
			.main = tempco_calibration_main
		},
		.name = "tempco-calibration",
		.help = "Output temperature and processed channel data as a CSV to ease temperature coefficient computation & calibration"
	};
