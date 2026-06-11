.. _service_flash_cbor_mib:

MIB stored in a flash device in a COSE-signed CBOR format
=========================================================

The ``flash-cbor-mib`` service reads a manufacturer information block (MIB) from a flash
partition and exposes it as a configuration (:c:type:`Conf`) subtree.

The partition contains a COSE_Sign1 structure (:rfc:`8152`) wrapping a CBOR document. On
initialisation the service:

#. reads the COSE_Sign1 structure from the configured :c:type:`Flash` partition,
#. verifies its Ed25519 signature against the configured public key,
#. parses the wrapped CBOR map record by record, recursing into nested maps, and
#. dynamically creates ``configlib`` nodes and links them together to mirror the same tree.

The resulting root is retrieved with :c:func:`flash_cbor_mib_get_root` and can be consumed by
any interface accepting a :c:type:`Conf` tree.

To stay within the tight RAM budget of an embedded target, the partition is **never** copied
into memory as a whole. CBOR is parsed directly from flash through a tinycbor reader callback,
and the signature is checked by streaming the payload through the incremental Ed25519 verify API
(``ed25519_verify_init`` / ``_update`` / ``_final``). The payload is read twice — once to hash it
for the signature, once to build the tree — instead of being buffered.

Payload format
--------------

The signed payload is a CBOR map representing the configuration tree. Each entry maps a node
name (the text-string key) to a value:

* a nested **map** becomes a subtree, recursed into;
* any other value becomes a leaf node whose configuration type is inferred from its CBOR
  encoding:

  =================  =============
  CBOR value         Conf type
  =================  =============
  unsigned integer   ``CONF_U32``
  negative integer   ``CONF_S32``
  float              ``CONF_F``
  boolean            ``CONF_B``
  text string        ``CONF_STR``
  byte string        ``CONF_BSTR``
  =================  =============

All nodes are created read-only (``CONF_READ``); the MIB is signed and therefore immutable.

Configuration
-------------

The service is configured through a :c:struct:`flash_cbor_mib_conf` passed to
:c:func:`flash_cbor_mib_init`: the :c:type:`Flash` partition, an optional offset and maximum
size, the Ed25519 public key and an optional name for the synthesized root subtree. The key may be
given as 32 raw bytes (``public_key``) or as a base64 string (``public_key_b64``, typically
forwarded verbatim from a port's Kconfig); the binary form takes precedence and the base64 form is
decoded only when the binary one is absent.

The service treats "a MIB is present in flash" and "a verification key is configured" as
independent prerequisites and decodes/validates the key itself, so a port can forward the
configured key blindly. The two mismatched cases — a MIB present without a key, and a key
configured without a MIB — are detected and logged distinctly.
