# E28-2G4T12S one-to-many implementation

This task implements the first-stage transparent broadcast/listen mode for
E28-2G4T12S modules.

## Radio roles

- Master: `APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_MASTER`
- Slave: `APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_SLAVE`

The master E28 address is `0xFFFF`, so it can broadcast and listen on the
shared channel. Each slave uses `APP_RADIO_SLAVE_BASE_ADDR + APP_RADIO_LOCAL_ID`
and must keep a unique local ID from `1..254`.

Default transparent E28 parameters:

```text
UART: 115200 8N1
Air rate bits: 0
Channel: 0x18
OPTION: 0x04
```

First-boot config frames:

```text
Master:  C0 FF FF 28 18 04
Slave 1: C0 00 01 28 18 04
Slave 2: C0 00 02 28 18 04
```

The flash marker is derived from role, local ID, E28 address, channel, option,
UART baud bits, and air-rate bits. Changing those values forces a new first-boot
configuration.

## MCU frame filter

When `APP_RADIO_FILTER_ENABLE=1`, radio input is parsed as:

```text
HEAD LEN TARGET SRC SEQ CMD PAYLOAD CRC
```

- `HEAD`: `0xA5`
- `LEN`: bytes from `TARGET` through payload
- `TARGET`: local device ID or `0xFF` broadcast
- `SRC`: source device ID
- `CRC`: 8-bit sum of all preceding bytes

Only valid frames whose `TARGET` is the local frame ID or `0xFF` are forwarded
to the local host UART. Other radio frames are dropped.

Slaves automatically ACK valid non-broadcast frames addressed to themselves.
The ACK frame uses the original `SEQ`, `CMD | 0x80`, and a one-byte status
payload of `0x00`.

The master records the latest non-broadcast host frame and clears that pending
ACK when a matching slave ACK arrives. Broadcast frames do not request ACKs.

## Serial configuration commands

The host UART (`USART2`) also accepts local ASCII configuration commands. These
commands are consumed by the MCU and are not forwarded to the radio.

Query current role, ID, and E28 address:

```text
CFG?
```

Set a slave ID and immediately reconfigure the E28 address:

```text
CFG ID 2
```

Valid slave IDs are `1..254`. The ID is saved in MCU Flash at the radio config
page. On the next boot, the saved ID overrides the compile-time
`APP_RADIO_LOCAL_ID`. Changing the ID invalidates the radio config marker, so
the firmware enters E28 command mode and writes the matching address again.

The command above makes the slave use:

```text
Frame ID: 0x02
E28 address: 0x0002
```

Master firmware rejects `CFG ID n` because the master address is fixed at
`0xFFFF` for broadcast/listen mode.

Host serial tools should send these commands as ASCII text. Normal application
frames should still be sent as binary/HEX bytes.

## Master transmit examples

The master host sends binary frames to `USART2`; the firmware forwards them to
E28 unchanged.

Send command `0x20` to slave 2:

```text
A5 04 02 01 10 20 DC
```

Broadcast command `0x20` to all slaves:

```text
A5 04 FF 01 11 20 DA
```

Send command `0x20` with payload `12 34` to slave 2:

```text
A5 06 02 01 10 20 12 34 24
```

The checksum is the 8-bit sum of all previous bytes in the frame.

## Build examples

```powershell
cd application
.\build_app.ps1 -Role master -LocalId 1
.\build_app.ps1 -Role slave -LocalId 1
.\build_app.ps1 -Role slave -LocalId 2
```

or:

```powershell
cd application
make APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_MASTER APP_RADIO_LOCAL_ID=1
make APP_RADIO_DEVICE_ROLE=APP_RADIO_ROLE_SLAVE APP_RADIO_LOCAL_ID=2
```

## Notes

This does not enable E28 fixed transmission mode (`OPTION bit7`). The wireless
payload remains transparent and unprefixed by E28 target address bytes.

One-to-many transparent radio does not prevent multiple slaves from transmitting
at the same time. The upper protocol must use polling, slots, or another
collision-avoidance rule for slave replies.
