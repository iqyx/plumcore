LoRa filesystem services
===================================================

LoRa modulation, and specifically the LoRaWAN protocol stack, is not well suited nor
designed for real time applications and applications requiring higher levels of
responsiveness. Most of the time it is the only available communication channel
on battery powered measurement devices.

One of the major concerns while designing such devices is the ability to do over-the-air
updates of the device firmware. Despite the fact LoRaWAN provides mechanisms for doing
such OTA updates (eg. multicast), not all LoRaWAN network providers support those features.

In some circumstances it is desirable to support OTA updates using simple send/receive
over LoRaWAN. Being small and simple, the feature set of this service is limited.


(anti)Features
------------------------

* number of concurrent transfers limited to 1
* file size limited to 65536 B
* file transfer encryption and MAC
* transfer is done in blocks of 16, 32, 64 or 128 bytes
* no compression is supported, files are meant to be already compressed


Main concepts
-------------------

The service uses a single LoRaWAN port for all its data. It doesn't transmit anything
proactively. If a message is received, it is parsed and a command is executed according
to its content. Following commands are supported:

* open file
* seek
* tell (generates response)
* write data
* read data (generates response)
* close file
* status report (generates response)
* read dir
* 



