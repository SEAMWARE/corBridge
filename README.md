# corBridge

The contract a **bridge plugin** satisfies.

A bridge is how the broker speaks to something that is not an NGSI-LD client: a
DDS topic, an MQTT broker, an OPC-UA server, a Modbus register. This library is
not that code. It is the seam — three headers of struct and enum, plus the
enum-to-string helpers that configuration parsing and logging need on both
sides of it.

`corPlugin` is the near relative: one is the *mechanism* for loading a `.so`,
this is the *contract* one kind of `.so` must satisfy.

## The three structs

`BRIDGE_ABI_VERSION` is **11**. Each slot is listed with the revision that added it.

| header | struct | direction | slots |
|---|---|---|---|
| `BridgeDriver.h` | `BridgeDriver` | broker → plugin: what the `.so` fills in | `alias`, `version`, `abiVersion`, `args`; ABI 1: `init`, `close`, `channelAdd`, `channelDel`, `publish`, `versionInfo`; ABI 2: `serviceInvoke`, `serverIface`; ABI 3: `serviceInvokeTracked`; ABI 4: `actionGoalSend`, `actionGoalCancel`; ABI 9: `channelAddInfo`; ABI 10: `notifySchemes`, `notify`; ABI 11: `serviceSchemes`, `serviceExecute`, `serviceCancel` |
| `BridgeBroker.h` | `BridgeBroker` | plugin → broker: what the plugin may call | `abiVersion`; ABI 1: `sampleIn`, `logFunction`; ABI 2: `sampleQualifiedIn`; ABI 3: `replyIn`; ABI 4: `goalEventIn`; ABI 5: `goalEventPartIn`; ABI 6: `sampleMetaIn`, `replyMetaIn`, `goalEventMetaIn`; ABI 7: `replyExchangeIn`; ABI 8: `endpointDiscoveredIn`; ABI 11: `serviceUpdateIn` |
| `BridgeServer.h` | `BridgeServer` | the peer side, returned by `serverIface()`: the broker never uses it; the functional test client does | `abiVersion`, `serviceServe`, `serviceUnserve`, `serviceReply` (ABI 2) |

ABI 9 and 10 added nothing to `BridgeBroker`. `channelDel` is not called by the
broker today: Channels are never removed while it runs.

A bridge `.so` exports exactly one symbol, `bridgeRegister`.

## Two properties worth knowing before writing a plugin

**The seam is plain data.** `const char*` and `int64_t`, in both directions. No
`KjNode`, no `CorAlloc`, no NGSI-LD type crosses it. That is what lets a plugin be
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
logged (INFO), not refused, and is not shown in `GET /version`, which carries
each bridge's `versionInfo()` string only.

The other direction, a plugin newer than the broker, is covered by a handshake:
the broker writes its own `BRIDGE_ABI_VERSION` into `BridgeDriver.abiVersion`
before calling `bridgeRegister`, the plugin fills in no slot the broker is too
old to have and then writes its own version there. A plugin checks
`brokerP->abiVersion` and that the pointer is not NULL before calling a
`BridgeBroker` slot added after ABI 1.

## Broker symbols

The broker executable exports its symbols and plugins are `dlopen`ed with
`RTLD_NOW`. So a C or C++ plugin may call the Cor-Libs the broker is built with
— corLog (`COR_E`, `COR_W`, `COR_V`, `COR_I`, `COR_T`), corAlloc, corJson,
corTree — without linking them; the loopback, MQTT, Modbus and DDS bridges log
with the `COR_*` macros and parse their configuration with corJson and corTree.
The way into the broker's NGSI-LD side is `BridgeBroker` and nothing else: no
function of the broker's own components is called directly.

`BRIDGE_ABI_VERSION` covers the three structs and nothing else. A plugin that
uses Cor-Lib functions must be built against the same Cor-Lib sources as the
broker that loads it (for a packaged broker: the `coraine-dev` source tarball of
the same version). A function the broker lacks fails the `dlopen`; a changed
signature or struct layout is not detected. A plugin that uses only the three
structs depends on `BRIDGE_ABI_VERSION` alone.

## Build

```sh
make
```

No dependencies. `CorArg.h` is included for the type of one pointer member; no
corArgs code is reached and no library is linked.

---

Copyright 2026 Seamware · Apache-2.0
