# Trichter BLE Interface

The Trichter device advertises a single custom GATT service over BLE. The device name is `Trichter-<DEVICE_ID>`.

## Service

| | UUID |
|---|---|
| **Trichter Service** | `12345678-1234-5678-9ABC-DEF012345678` |

## Characteristics

| Name | UUID | Properties |
|---|---|---|
| Status | `12345678-1234-5678-9ABC-DEF012345679` | Read, Notify |
| Result | `12345678-1234-5678-9ABC-DEF01234567A` | Read, Indicate |
| Control | `12345678-1234-5678-9ABC-DEF01234567B` | Write Without Response |
| Image | `12345678-1234-5678-9ABC-DEF01234567C` | Read, Notify |

---

### Status (`...79`)

A single byte reflecting the current session state.

| Value | Name | Meaning |
|---|---|---|
| `0x00` | `IDLE` | No session in progress |
| `0x01` | `WAITING` | Waiting for flow to start |
| `0x02` | `RUNNING` | Session active, measuring flow |
| `0x03` | `COMPLETE` | Session finished, result available |
| `0x04` | `ERROR` | An error occurred |

Subscribe to **notifications** to receive status changes in real time. You can also read the value at any time.

---

### Result (`...7A`)

A packed binary struct sent as an **indication** when a session completes. The device waits for an `ACKNOWLEDGE` command before clearing the result and transitioning back to `WAITING`.

#### Format (13 bytes, little-endian)

| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 4 | `uint32` | `duration_ms` — session duration in milliseconds |
| 4 | 4 | `float32` | `rate_lpm` — average flow rate in litres per minute |
| 8 | 4 | `float32` | `volume_l` — total volume in litres |
| 12 | 1 | `uint8` | `has_image` — `1` if an image is available, `0` otherwise |
| 13 | 4 | `uint32` | `image_size` — JPEG image size in bytes (0 if no image) |

Total: **17 bytes**

Subscribe to **indications** to be notified when a result arrives. Reading the characteristic also returns the current result if one is available (returns an error otherwise).

---

### Control (`...7B`)

Write a single command byte to control the device. No response is sent.

| Value | Command | Effect |
|---|---|---|
| `0x02` | `ACKNOWLEDGE` | Acknowledge the current result; device transitions back to `WAITING` |
| `0x03` | `RESET` | Reset the device |
| `0x04` | `FAKE_RUN` | Trigger a simulated session (for development/testing) |

---

### Image (`...7C`)

The captured JPEG image is transferred by reading this characteristic repeatedly. Each read returns up to **20 bytes** of image data, advancing an internal offset. Read sequentially until `image_size` bytes have been received.

Subscribe to **notifications** if you want to be notified when image data becomes available.

> The offset resets on disconnect or when the result is cleared via `ACKNOWLEDGE`.

---

## Typical Interaction Flow

```
Client                              Trichter
  |                                    |
  |--- Connect ----------------------->|
  |--- Subscribe: Status (notify) ---->|
  |--- Subscribe: Result (indicate) -->|
  |                                    |
  |<-- Notify: Status = WAITING -------|  (waiting for flow)
  |<-- Notify: Status = RUNNING -------|  (flow detected)
  |<-- Notify: Status = COMPLETE ------|  (flow stopped)
  |<-- Indicate: Result --------------->|  (result payload)
  |                                    |
  |--- Read: Result (optional) ------->|  (re-read if needed)
  |                                    |
  |  [if has_image == 1]               |
  |--- Read: Image (repeat) ---------->|  (read 20 bytes at a time)
  |--- Read: Image (repeat) ---------->|  (until image_size bytes received)
  |                                    |
  |--- Write Control: 0x02 (ACK) ----->|  (acknowledge result)
  |<-- Notify: Status = WAITING -------|  (ready for next session)
```
