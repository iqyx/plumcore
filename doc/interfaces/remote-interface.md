# Remote interface

A RemoteInterface is a piece of functionality meant to be used by a remote peer (a flash updater, a message queue
poller, a configuration tree accessor, ...). It is transport agnostic and contains no addressing at all - no service
ID, no endpoint number, no characteristic UUID. Instead it provides:

- a description of itself as structured data (`struct remote_interface_desc`), which a transport encodes in its own
  way (an nbus2 descriptor advertisement, a BLE descriptor characteristic, a CLI listing, ...)
- a session management: a transport opens a session for each remote peer (or each connection) it serves and closes it
  when the peer goes away
- a message based data path: the transport pushes every message received from the peer to the interface (`write`)
  and pulls messages the interface wants to send to the peer (`read`)

The other side of the contract is a transport (a communication channel implementer, eg. an nbus2 exporter, a BLE GATT
exporter or a framed serial line). A transport discovers all RemoteInterface instances advertised in the service
locator (`ISERVICELOCATOR_TYPE_REMOTE_INTERFACE`), assigns them transport-specific addresses, advertises their
descriptions and moves the data. As the addressing lives entirely in the transport, a single RemoteInterface may be
exported over several transports at the same time without creating more instances - each transport simply opens its
own sessions.

The interface is declared in `services/interfaces/remote-interface.h`. The methods, return codes and description
fields are documented there.


## Data model

The data path is message oriented, the same as the Datagram interface: a single `write` or `read` call always carries
a whole message of at most `desc->mtu` bytes. Stream transports (a serial line) are expected to frame the messages,
transports with a smaller native MTU (BLE) are expected to fragment them.

Received messages are pushed by the transport (`write`), messages to be sent are pulled by the transport (`read`).
Pulling lets the transport own the transmit buffer and pace the transmission according to its own flow control (BLE
notification credits, nbus2 polling, serial line throughput). A message to send may be produced without any preceding
received message (notifications, streamed data).

To avoid polling, the transport passes a Notify interface to `open`. The interface calls it whenever a new message is
queued for reading or the session is closed by the interface, the notification value is the number of messages
waiting to be read. Using the notification is optional, `write` and `read` always block for up to the given timeout.
A transport which must not block either waits for a notification and then calls `read` with a zero timeout until it
returns `REMOTE_INTERFACE_RET_TIMEOUT` or `REMOTE_INTERFACE_RET_CLOSED`, or simply polls with a zero timeout.


## Delivery guarantees

Message delivery is best effort. A transport guarantees a message is delivered whole or not at all (never truncated,
merged with another one or corrupted), but a message may be lost (a dropped nbus2 packet, a `write` not accepted
within its timeout, a disconnected link) and on connectionless transports it may also be reordered or duplicated. The
protocol spoken over the interface must tolerate it (eg. by using request/response pairs with retransmissions,
sequence numbers or idempotent requests).


## Sessions and locking

Every data transfer happens within a session. A session is an opaque pointer returned by the interface in `open`,
pointing to one of its own preallocated session slots, so no session state is allocated by the transport. The
interface keeps its per-peer protocol state (an ongoing flash update, a subscribed topic, a retransmission window,
...) in the session, therefore two transports or two peers never interfere.

Connection oriented transports map sessions to connections directly (open on connect, close on disconnect).
Connectionless transports (nbus2) map a remote address to a session themselves and close the session after
`desc->session_idle_ms` of inactivity.

Locking is an internal concern of the interface: an interface which cannot serve concurrent sessions either limits
`desc->max_sessions` to 1, or refuses `open` while another session holds a lock (eg. during a flash update).


## Closing a session

A session is closed and its slot freed only by `close` called by the transport. Every successful `open` must be
paired with exactly one `close`, regardless of which side initiated the closing. This guarantees a session pointer
held by a transport never refers to a slot already reused for another peer.

When the transport closes a session (the peer disconnected, the session idled out), the interface aborts any
unfinished multi-step operation as if the peer had vanished, releases any lock held by the session, drops all pending
messages and frees the slot. Completed side effects are not rolled back (a written flash block stays written), the
protocol defines what an abort means. After `close` returns, the session pointer is invalid and must not be used
anymore.

When the interface doesn't want to communicate anymore (logout, internal timeout, a lock taken over), it signals it to
the transport:

- the protocol state is torn down and locks are released immediately, so other sessions are not held back until the
  transport reacts
- messages already queued to send (eg. a logout reply, an error explaining the closing) are still returned by `read`.
  Afterwards every `write` and `read` returns `REMOTE_INTERFACE_RET_CLOSED` and any call blocked on the session is
  woken up with the same result
- the Notify interface passed to `open` is called
- the slot stays allocated (and counts against `desc->max_sessions`) until the transport calls `close`

On `REMOTE_INTERFACE_RET_CLOSED` the transport notifies the peer in its own way (drops the BLE link, ...), as the
interface has no addressing to do it itself, and calls `close`.


## Threading

All methods are thread safe. Different sessions may be served from different tasks (each transport runs its own task)
and `write` and `read` of the same session may be called concurrently. All blocking methods take a timeout - a
transport calling from a context which must not block (eg. the BLE rx task) passes a zero timeout. The session
lifetime is owned by the transport: `close` must not be called concurrently with any other call on the same session
(including a call blocked in `write` or `read`), therefore the interface never has to wait for calls in progress.
