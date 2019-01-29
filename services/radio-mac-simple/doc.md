radio scheduler task:
- processes the input queue one by one, waits for a specified time instant
  and enables radio receiver/transmitter.
- when a packet is received, put it to a rx process queue (not parsed yet)
- when a packet has to be sent, query the tx queue, get packet raw data and
  transmit it at a specified time instant
- when there is no rx/tx window specified in the future, compute the time
  the radio is allowed to sleep
- if a packet is posted on the queue when the radio is in the sleep mode,
  the radio has to be woken up
