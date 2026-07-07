=================================================================
NBUS CAN-FD sensor bus driver
=================================================================


Data link layer encryption
===========================

nbus2 packets are authenticated and encrypted at the data link layer with a SIV-mode
(synthetic IV) authenticated-encryption construction built from a *single* cryptographic
primitive: BLAKE2s (RFC 7693). The same primitive, keyed, provides both the message
authentication code (which doubles as the synthetic IV) and, in a counter mode, the
stream-cipher keystream. The implementation lives in ``blake2s-siv.c`` (``b2s_derive_keys``,
``b2s_siv``, ``b2s_crypt``).

The scheme follows the generic SIV / S2V shape (Rogaway–Shrimpton deterministic
authenticated encryption, cf. RFC 5297) instantiated with non-AES primitives: the keyed
hash is the pseudorandom function that produces the tag, the tag is used as the IV, and the
plaintext is then encrypted under that IV. As a result a repeated "nonce" never breaks
authenticity, and confidentiality degrades gracefully rather than catastrophically on reuse.


Design goals
------------

- **One primitive only.** BLAKE2s is the sole cryptographic building block. There is no
  separate cipher and no separate MAC algorithm to carry, which keeps the code and the
  attack surface small on a constrained MCU.
- **Short, fixed overhead.** The synthetic IV is truncated to 64 bits and is the only
  per-packet cryptographic overhead (8 bytes). No explicit nonce or separate MAC tag is
  transmitted.
- **Online protection.** The threat model is an online, real-time attacker on the bus. The
  construction is sized to resist online forgery and tampering, not to provide long-term
  confidentiality (see `Security considerations`_).
- **No key agreement.** A single preshared key is used; there is no handshake and no
  periodic key exchange (see `Security considerations`_).


Keys and key derivation
------------------------

All packets are protected with two 128-bit subkeys derived once from a single, arbitrary
length preshared key ``K`` (``b2s_derive_keys``)::

    (ke || km) = BLAKE2s(K)        # 32-byte keyless digest, split into two halves

==========  =======  ============================================================
subkey      length   role
==========  =======  ============================================================
``ke``      16 B     keystream (encryption) key
``km``      16 B     MAC / synthetic-IV key
==========  =======  ============================================================

The derivation is a keyless BLAKE2s of the preshared key whose 32-byte output is split into
the encryption key ``ke`` (first 16 bytes) and the MAC key ``km`` (last 16 bytes). Because
BLAKE2s emits at most 32 bytes, deriving two independent keys from one preshared key yields
128-bit subkeys, which is why the keystream key is 128 bits.


Packet protection
-----------------

The synthetic IV is a truncated keyed BLAKE2s over the cleartext header and payload
(``b2s_siv``)::

    SIV = BLAKE2s(key = km, header || plaintext)[:8]      # 64-bit tag, doubles as the IV

The keystream is produced by BLAKE2s in counter mode, keyed with ``ke`` and using the SIV as
the IV (``b2s_crypt``)::

    keystream = BLAKE2s(key = ke, SIV || ctr=0) ||
                BLAKE2s(key = ke, SIV || ctr=1) || ...     # 32 bytes per block

    ciphertext = (header || plaintext) XOR keystream

``ctr`` is a four-byte big-endian block counter starting at zero. Each 32-byte BLAKE2s block
yields 32 keystream bytes; the header and payload are encrypted as one contiguous region.
The SIV itself is transmitted in the clear (it is the IV) and authenticates everything after
it.


Packet format
-------------

A protected packet is a fixed 24-byte header followed by the payload. Only the SIV is in the
clear; the fixed header and the payload are encrypted.

======  ======  ==========  ========================================================
offset  size    state       field
======  ======  ==========  ========================================================
0       8       clear       SIV — synthetic IV / 64-bit MAC tag
8       2       encrypted   magic ``'n2'``
10      2       encrypted   payload length (big endian)
12      2       encrypted   transmit counter (big endian)
14      1       encrypted   ``(dst_ep << 4) | src_ep``
15      1       encrypted   flags
16      4       encrypted   destination service ID
20      4       encrypted   source service ID
24      ...     encrypted   payload
======  ======  ==========  ========================================================

The SIV authenticates the cleartext bytes from offset 8 to the end of the payload (the fixed
header plus the payload).

The flags field at offset 15 carries per-packet bit flags. The only flag currently defined is
bit 0 (``NBUS_FLAG_MULTICAST``): when set, the packet is a multicast packet and is delivered to
multicast sockets only.


Transmit
--------

#. Assemble the cleartext fixed header (offsets 8..23) and copy the payload (offset 24..).
#. Compute ``SIV = BLAKE2s(km, buf[8:])`` and write its first 8 bytes to offsets 0..7.
#. Encrypt ``buf[8:]`` in place with the keystream derived from the SIV.
#. Hand the packet to the medium-access / framing layer for transmission.


Receive
-------

#. Decrypt ``buf[8:]`` in place using the cleartext SIV at offsets 0..7 (the keystream is a
   deterministic function of the SIV and ``ke``).
#. Verify the ``'n2'`` magic and that the declared payload length fits the received frame.
#. Recompute the SIV over the now-cleartext header and payload and compare it, in constant
   time, with the SIV received in the packet. Discard the packet on any mismatch.

Because encryption is an XOR of a deterministic keystream, a failed decryption attempt can be
undone (by re-applying the same keystream) so that another protection scheme can be tried on
the original bytes — receivers attempt the schemes they support newest first, while a sender
emits a single configured scheme.


Security considerations
-----------------------

The explicit goal of this layer is to provide online, real-time protection using a **single
cryptographic primitive (BLAKE2s)** and a **synthetic IV of a manageable length (64 bits)**,
while keeping the per-packet overhead small. It is an obstruction / integrity layer, not a
provider of strong, long-term confidentiality; applications requiring the latter must add it
at a higher layer.

**No key agreement, single preshared key.** There is no handshake, no key agreement and no
periodic key exchange. Every node uses the same static preshared key, from which ``ke`` and
``km`` are derived once. The security of the whole layer therefore reduces to the entropy and
the management of that single preshared key: it must be high-entropy and should be rotated by
out-of-band means. The built-in default key (``"abcd"``) is for development only and must be
replaced in any real deployment.

**64-bit SIV — a deliberate, asymmetric trade-off.** The 8-byte SIV plays two roles with two
different security bounds:

- *As a MAC tag* it gives a forgery probability of 2\ :sup:`-64` per attempt. Crucially there
  is no offline grinding: a forgery requires a live interaction on the bus for every attempt,
  so 64 bits is comfortable against an online attacker — which is exactly the threat model.
- *As an encryption IV* it bounds confidentiality at the birthday limit. Two distinct packets
  whose 64-bit SIVs collide reuse the same keystream, leaking the XOR of that pair of
  plaintexts. Such a collision becomes likely after roughly 2\ :sup:`32` packets sent under
  one key. This is the dominant confidentiality horizon and the reason the layer is not meant
  for long-term secrecy. It is acceptable here because protection is online and real-time, and
  because the key can be rotated before that horizon is approached.

**Deterministic-AE leakage.** As with any SIV/DAE construction, the SIV is a public
deterministic function of the plaintext: identical ``header || payload`` inputs produce an
identical SIV and identical ciphertext, which reveals that two messages are equal. The 16-bit
transmit counter in the header varies the input so that genuine repeats differ, but it wraps
every 65536 packets.

**Keystream block counter.** The keystream block counter is effectively one byte, so the
keystream repeats after 256 blocks (8 KiB) within a single packet. The nbus2 MTU is far below
this, so it is not reached in practice; it nonetheless caps the maximum protected packet size.
