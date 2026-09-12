# EP-73c: Login manager implementation

> [!NOTE]
> EP status is Draft

For the purpose of local debugging and control, devices usually have a serial diagnostic console port. In order to use a common set of software tools the protocols used should be made standard and there should be a common gateway awaiting and (possibly) filtering any user input. A new service called `loginmgr` to handle any user-facing diagnostic port communication is proposed.

## Motivation

Historically, a basic `loginmgr` service always existed to allow users to log into the system console with a hardcoded login credentials. This is obviously inadequate from the security standpoint but it at least filtered out the most basic "attacks" made by curious users. Over time a bunch of functionality has been added to the plumCore framework which can expose a possibility for the user to harm the device which needs a more sophisticated access control, eg:

- manipulating the key store - deleting keys and certificates, changing public keys for authenticating remote systems, etc.
- updating the firmware
- changing calibration constants

Apart from security, there are functional features which arose from the field use and are in the *nice to have* group:

- multiplexing the access to multiple handling services
- possibility of switching user/machine (or API) access

## Feature specification

### Accessing the diagnostic console port

Upon connection to the serial port, the user usually cannot see anything on the terminal emulator. The automatic reaction, as usually seen in this case, is to press various keys until something happens. Respecting the *principle of the least surprise*, `loginmgr` *must not* react to cat-typing when not logged in. It *should* greet the user appropriately, wait for the user to calm down (eg. wait until the typing stops) and display the prompt afterwards. Optionally, it *can* detect noise on the receive line when the reception of characters wouldn't stop and display an appropriate warning to the user. The behaviour should be bound to a configurable timer to greet the user if they try to access the console again.

### Wakelock

When the user is greeted after accessing the console, `loginmgr` *must* obtain a preconfigured wake lock in case a low power mode is enabled. This will allow the user to access the diagnostic console at the nominal speed without the communication being dropped or otherwise damaged by entering low power modes.

### Channels and multiplexing

`loginmgr` is the appropriate service to implement an user-facing functionality to multiplex multiple *channels*, each channel possibly using a different communication protocol. This functionality is optional and should be configurable in compile time. The functionality is designed as follows:

- On the first user access, a new *default channel* is created. The *default channel* *should* be a text console readable and usable by a human user. The user should be noticed about the channel created.
- At any time it *must be* possible to create a new channel or close the current one, regardless of the channel protocol or state of the channel (whether the user is logged in the channel or not).
- It *must be* possible to switch the current channel endpoint and protocol when the user is not logged in.
- It *must be* possible to switch the current channel in all circumstances, whether logged in or not, regardless of the protocol used.

The recommended implementation behaves in this way:

- when the user is greeted, the list of channels is displayed and a new default channel is automatically created and made active
- `ctrl+n` creates a new channel and makes it active using the default protocol (user readable). To catch other common shortcuts, it responds to `ctrl+t` (new tab) too
- `ctrl+d` logouts the current channel without closing the login prompt. When `ctrl+d` is pressed again, the login prompt is closed too
- `ctrl+tab` switches to the next channel, `ctrl+shift+tab` switches to the previous channel
- `ctrl+1` to `ctrl+9` switches to the corresponding channel
- `ctrl+x` changes protocol of the channel. This works only when the channel is created and logged out
- when a channel is switched, created/closed or a channel protocol is changed, a list of channels (with the active one being highlighted) is displayed again and a protocol dependent refresh handler is called causing the optional command prompt to be shown again.

Example:

```text
Greeting! Press any key to activate this console.
<enter>
<enter>

plumCore diagnostic console -- [channel 1: CLI]
login: admin
password: ******

--- display issue here ---

cli / > quit

plumCore diagnostic console -- [channel 1: CLI]
login:
```

### Multiplexing using escape sequences

The implementation *should* support the same functionality using escape sequences, that is channel switching, getting the list of channels including the active one, channel creation and closing, protocol changing.

Login prompt *should* be marked with escape sequences too to ease parsing on the client machine side in cases there is a software used for communication instead of an user with a terminal emulator.

### Stream protocols

As the serial terminal is a `Stream` device, accessing the channel protocol implementation using the same `Stream` interface *should be* fully transparent with the exception of understanding the previously mentioned key shortcuts. As the `Stream` interface doesn't provide any framing, the `loginmgr` implementation is free to split the `Stream` method calls as required.

### Datagram protocols

For accessing the device API and other purposes it *must be* possible to use a channel protocol implementation using the `Datagram` interface. This poses multiple problems, namely lack of fragmentation on the `Stream` interface which datagram protocols require and occurences of special characters within datagrams which may have special meaning on the serial console.

For this purpose a special encapsulating protocol is required which provides framing for datagrams, escapes special characters and additionally provides some basic error checking.

### Logout after inactivity

If no activity is detected on the `loginmgr` for a specified amount of time, it *should* logout all channels and release the wake lock.
