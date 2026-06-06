==========================================================
proto-dgtext: datagrams over a Stream as plain text lines
==========================================================

The ``proto-dgtext`` service carries arbitrary binary datagrams over a plain,
character-oriented ``Stream`` (a UART, a TCP connection, a console pipe, ...).
Each datagram is turned into a single line of printable ASCII terminated by a
newline, so it survives transports that can only move ordinary text and stays
readable in a terminal or a log.

It is the text-line counterpart of ``proto-stream`` (which does the opposite,
carrying stream data over datagrams): ``proto-dgtext`` consumes a ``Stream`` and
exposes a ``Datagram`` interface for sending and receiving whole datagrams.


Wire format
===========

Every datagram is sent as one line::

    $<base64 payload>*<checksum>\n

==========  ====================================================================
``$``       Synchronization token. The receiver discards everything until it
            sees this character before it starts collecting a new datagram.
payload     The raw datagram bytes, base64-encoded. Contains no whitespace.
``*``       Delimiter separating the payload from the trailing checksum.
checksum    The first 4 bytes of a BLAKE2s digest of the *raw* datagram
            payload, lowercase hex (8 characters).
``\n``      End of line, terminates the frame.
==========  ====================================================================

Example line for the 3-byte payload ``{0x01, 0x02, 0x03}``::

    $AQID*1a2b3c4d\n

Every character on the wire is printable ASCII.


Checksum
========

The trailer uses **BLAKE2s** (RFC 7693) truncated to 4 bytes rather than a CRC.
BLAKE2s is a cryptographically strong keyless hash that is still cheap on a
constrained MCU: a single small context, no lookup tables. Truncating to 4 bytes
keeps the line short while giving a far stronger integrity check than a CRC-16.
The digest is computed over the raw payload bytes, identically on both ends, so
the sender's incremental hashing and the receiver's verification always agree.

The checksum length is fixed by ``PROTO_DGTEXT_CKSUM_BYTES`` (4) in the header.


Usage
=====

Initialise the service on top of an existing ``Stream`` and obtain the
``Datagram`` interface used for both directions:

.. code-block:: c

    ProtoDgtext dgtext;
    proto_dgtext_init(&dgtext, my_stream);

    Datagram *d;
    proto_dgtext_get_datagram(&dgtext, &d);

Send a datagram:

.. code-block:: c

    uint8_t payload[] = {0x01, 0x02, 0x03};
    d->vmt->write(d, payload, sizeof(payload), NULL);

Receive a datagram. ``read`` blocks the calling thread until a complete, valid
line arrives; ``len`` is the buffer capacity on entry and the decoded length on
return:

.. code-block:: c

    uint8_t buf[256];
    size_t len = sizeof(buf);
    if (d->vmt->read(d, buf, &len, NULL) == DATAGRAM_RET_OK) {
        /* buf holds len decoded bytes */
    }

There is no addressing in this transport, so the ``struct datagram_msg``
argument may be ``NULL``; on receive it is zero-filled.


Behaviour and threading
========================

- **No receive task.** ``read`` runs entirely on the caller's thread and decodes
  straight from the stream, so the service needs no background thread of its own.
  Inbound bytes are pulled from the stream in chunks (up to
  ``PROTO_DGTEXT_RX_CHUNK``, 32 bytes) to amortize the stream read calls, then
  consumed one at a time. Bytes read past a frame's newline belong to the next
  datagram and are retained between ``read`` calls.
- **Streaming transmit.** ``write`` emits the line directly to the stream: the
  sync token, then the payload base64-encoded in small chunks, then the trailer.
  The checksum is computed incrementally while encoding, so no full-line buffer
  is allocated.
- **Locking.** A transmit mutex serializes concurrent writers and a receive
  mutex serializes concurrent readers (the latter share the persistent read
  buffer). Transmit and receive are independent, so a read and a write may run
  in parallel.
- **Error handling.** A corrupt or malformed line (bad base64, wrong length,
  checksum mismatch) makes the current ``read`` return ``DATAGRAM_RET_FAILED``.
  A stray sync token mid-line resynchronizes to the new frame.


Configuration
=============

Kconfig (``Presentation layer protocols``):

``SERVICE_PROTO_DGTEXT``
    Enable the service.

``SERVICE_PROTO_DGTEXT_MAX_DATAGRAM_LEN``
    Maximum datagram payload size in bytes (default 256). On transmit, larger
    datagrams are rejected with ``DATAGRAM_RET_BAD_ARG``. On receive, the actual
    bound is the buffer the caller passes to ``read``.
