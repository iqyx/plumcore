===========================================
EP-235: DAQ data package
===========================================

.. image:: https://img.shields.io/badge/Status-Draft-blue

.. image:: https://img.shields.io/badge/plumCore-0.8.0--dev-gray?labelColor=purple



TBD introduction


The data format
=====================

Top level format structure is a CBOR map with indefinite length with a ``PPKG = 1`` key/value
pair at the beginning. ``PPKG`` is an abbreviation for *plumCore package*, ``1`` is the current
format version. This results in the following encoding:

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

