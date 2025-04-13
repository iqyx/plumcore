===========================================
EP-235: DAQ data package
===========================================

.. sectnum::
	:suffix: .

.. image:: https://img.shields.io/badge/Status-Draft-blue

.. image:: https://img.shields.io/badge/plumCore-0.8.0--dev-gray?labelColor=purple


.. contents:: 


Introduction
=====================

As the plumCore framework continues to develop into a generic DAQ platform, managing data processing,
transmission, and storage is becoming increasingly challenging. To address this, it would be beneficial
for the platform to adopt a universal, generic storage format that can be used across various media, including:

- FIFO buffers in RAM for short-term storage
- FIFO buffers in flash for long-term, power outage-resistant storage
- Message queue-like transports such as MQTT and CoAP
- Long-term archiving solutions

This storage format should be capable of containing the following information:

- A single sensor value or a time series (measurement)
- The time at which the measurement was taken, along with relevant time metadata (e.g., clock precision, clock type)
- Metadata related to the sensor value or time series (e.g., uncertainty, conditions)
- Sampling strategy details (e.g., arbitrary intervals, equal intervals)
- Calibration data used during sample processing
- Optionally, the raw sensor value
- Identification of the data source
- Electronic signature and, optionally, encryption

Having a common storage format would simplify data management and facilitate seamless data exchange
across different media and applications.

Key-word usage
--------------------

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT",
"SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT RECOMMENDED", "MAY", and
"OPTIONAL" in this document are to be interpreted as described in
BCP 14 [RFC2119] [RFC8174] when, and only when, they appear in all
capitals, as shown here.

License
---------------

This work is licensed under CC BY-SA 4.0. To view a copy of this license, visit https://creativecommons.org/licenses/by-sa/4.0/

© 2025 Marek Koza <qyx@krtko.org>


The data format
=====================

Package, the top-level data structure
-----------------------------------------

Top level data structure is a CBOR map with indefinite length with a ``PPKG = 1`` key/value
pair at the beginning. It is called a *package*. ``PPKG`` is an abbreviation for *plumCore package*,
``1`` is the current format version. This results in the following encoding:

.. code-block:: python

	0xbf # indefinite length map
		0x65 # UTF-8 test of length 5
			0x50 0x50 0xfb 0x47 0x20 # "PPKG "
		0x01 # Unsigned 1
		# payload goes here as key/value pairs
		0xff # End of map

This structure allows maintaining a constant 8 byte string at the beginning of a file or datagram
allowing tools to automatically identify the format and version.

In case a ``COSE`` signature is used, the header changes:

.. code-block:: python

	0xbf # indefinite length map
		0x65 # UTF-8 test of length 5
			0x43 0x50 0xfb 0x47 0x20 # "CPKG "
		0x01 # Unsigned 1
		0x01
		0xbf
			# COSE headers
			0xff
		0x02 
			# COSE payload goes here encoded as a byte string
		0x03
			# COSE signature goes here
		0xff # End of map


Using an indefinite length CBOR map to encode a package has an interesting property.
Since it uses the ``0xff`` byte to finish the map, we may freely append more than one
package one after the other in a common flash memory media which contains
``0xff`` bytes when erased. However, beginning of a package needs to be aligned to
a 8 byte boundary in the media region (RAM region, file, flash media partition).
The padding between the successive packages SHALL be filled with ``0xff`` bytes.
