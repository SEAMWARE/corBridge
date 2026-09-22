# corBridge

The contract a **bridge plugin** satisfies.

A bridge is how the broker speaks to something that is not an NGSI-LD client: a
DDS topic, an MQTT broker, an OPC-UA server, a Modbus register. This library is
not that code. It is the seam — two headers of struct and enum, plus the
enum-to-string helpers that configuration parsing and logging need on both
sides of it.

`corPlugin` is the near relative: one is the *mechanism* for loading a `.so`,
this is the *contract* one kind of `.so` must satisfy.

## The two sides

| header | direction | holds |
|---|---|---|
| `BridgeDriver.h` | broker → plugin | what the `.so` fills in: `init`, `close`, `channelAdd`, `channelDel`, `publish` |
| `BridgeBroker.h` | plugin → broker | what the plugin may call: `sampleIn`, `logFunction` |

A bridge `.so` exports exactly one symbol, `bridgeRegister`.

## Two properties worth knowing before writing a plugin

**The seam is plain data.** `const char*` and `int64_t`, in both directions. No
`KjNode`, no `KAlloc`, no NGSI-LD type crosses it. That is what lets a plugin be
written in something other than C — the DDS bridge is C++, because the library
it wraps has an API of `std::string` and `std::shared_ptr` that cannot be
reached from C at all — and it is what keeps the broker's allocator away from
threads the broker did not create.

**A bridge knows nothing about entities.** It carries bytes to and from an
endpoint. Which entity attribute an endpoint corresponds to is a Channel, and
Channels live in the broker. So the seam speaks `(endpoint, json, time)` in both
directions and nothing else: inbound through `sampleIn()`, outbound through
`publish()`.

## Compatibility

Bridge plugins are external shared objects, built at other times against other
checkouts. `BRIDGE_ABI_VERSION` is bumped on every change, and the structs are
**append-only**: never reordered, never resized, never repurposed. The broker
owns the allocation of both, so an older plugin simply leaves the newer slots
NULL — which is already the encoding for "not supported". A version mismatch is
logged, not refused.

## Build

```sh
make
```

No dependencies. `KArg.h` is included for the type of one pointer member; no
kargs code is reached and no library is linked.

---

Copyright 2026 Seamware · Apache-2.0
