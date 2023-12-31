====================================
Power source/sink device interface
====================================


A power device is any device in the system able to source or sink current
with these additional features:

* it reports the voltage applied on its input or output. When the device
  is powered off, the voltage should read as zero.
* it measures the current flowing through the device bidirectionally
* it allows the client to manage its conduction state (ie. to power it up
  or down)
