==================================================================
NVM (non-volatile memory) key-value store in a flash partition
==================================================================


key-length-value-mac quardruplets

Block headers:

- 0xff empty block record header (no continuation)
- 0x00 void record header (no continuation)
- active block header
- full block header


Record headers:

- first block signature
- key->value pair


Example
==================

Block 1:
- full block
- first block signature
- key-value pair
- key-value pair
- key-value pair
- key-value pair
- key-value pair
- key-value pair
- void
- void
- void

Block 2:
- active block
- first block signature
- key-value pair
- key-value pair
