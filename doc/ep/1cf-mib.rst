=========================================================
EP-1cf: Manufacturer information block structure (MIB)
=========================================================

.. sectnum::
	:suffix: .

.. image:: https://img.shields.io/badge/Status-Draft-blue

.. image:: https://img.shields.io/badge/plumCore-0.8.0--dev-gray?labelColor=purple


.. contents::


This document proposes the introduction of a Manufacturer Information Block (MIB) to be stored in the microcontroller
flash, containing data populated by the manufacturer during the production process, typically during End-Of-Line (EOL)
testing and firmware flashing. The MIB is envisioned as a tree-structured data object, utilizing the Concise Binary
Object Representation (CBOR) format, with various keys to store relevant information, including but not limited to:
unique serial number, configuration settings for optional features, generated random seeds, cryptographic key pairs
(private and public keys), associated certificates, date of manufacture, and version information for the hardware.
To ensure the authenticity and integrity of the MIB, it is recommended to be optionally signed using the CBOR Object
Signing and Encryption (COSE) protocol. The proposed MIB aims to provide a standardized and secure method for storing
manufacturer-specific data, enabling improved device identification, authentication, and supply chain management,
while also facilitating the implementation of advanced security features and device lifecycle management.


Introduction
=====================

TBD


Key-word usage
--------------------

The key words "MUST", "MUST NOT", "REQUIRED", "SHALL", "SHALL NOT",
"SHOULD", "SHOULD NOT", "RECOMMENDED", "NOT RECOMMENDED", "MAY", and
"OPTIONAL" in this document are to be interpreted as described in
BCP 14 [RFC2119] [RFC8174] when, and only when, they appear in all
capitals, as shown here.

License
---------------

This work is licensed under CC BY-SA 4.0.
To view a copy of this license, visit https://creativecommons.org/licenses/by-sa/4.0/

© 2025 Marek Koza <qyx@krtko.org>


MIB content
=======================

Product name ``pn`` (``1``)
----------------------------------

Product name is a unique identifier within the manufacturer's namespace that distinguishes a product
with a specific set of features and functionality. Products with the same product name are expected to be variants
of the same product line, with each new version building upon the previous one, and maintaining a consistent set
of characteristics and use cases.

Product name is saved as an UTF-8 string.


Manufacturer ``mfg`` (``2``)
----------------------------------

Manufacturer of the product, product line or a trade mark

Saved as a UTF-8 string.



