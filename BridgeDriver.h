#ifndef CORBRIDGE_BRIDGEDRIVER_H_
#define CORBRIDGE_BRIDGEDRIVER_H_

//
// FILE            BridgeDriver.h
//
// AUTHOR          Ken Zangelin
//
// Copyright 2026 Seamware
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdint.h>                                   // int64_t, uint64_t

#include "kargs/KArg.h"                               // KArg

#include "corBridge/BridgeBroker.h"                   // BridgeBroker, BRIDGE_*
#include "corBridge/BridgeServer.h"                   // BridgeServer



// -----------------------------------------------------------------------------
//
// BRIDGES_MAX - how many bridge plugins may be active at once
//
// Bridges are few by construction: one per transport the deployment speaks.
// The limit exists to keep the registry a plain array.
//
#define BRIDGES_MAX  8



// -----------------------------------------------------------------------------
//
// BridgeChannelKind - what shape of interaction an endpoint has
//
// Declared in full from the outset although only BridgeChannelTopic is
// implemented today. A Channel carries its kind from the day it is created, so
// that adding services and actions later is a matter of filling in vtable slots
// rather than migrating stored objects that never said what they were.
//
//   Topic    - a value is published and consumed. One direction per sample.
//   Service  - a request is sent and exactly one reply comes back.
//   Action   - a goal is sent, then runs: feedback, status, a result, and it
//              can be cancelled while in flight.
//
typedef enum BridgeChannelKind
{
  BridgeChannelTopic    = 0,
  BridgeChannelService  = 1,
  BridgeChannelAction   = 2
} BridgeChannelKind;



// -----------------------------------------------------------------------------
//
// BridgeDirection - which way values move on a Channel
//
//   In    - the foreign endpoint produces, the broker consumes
//   Out   - the broker produces, the foreign endpoint consumes
//   Both  - both of the above on one endpoint
//
typedef enum BridgeDirection
{
  BridgeDirectionIn    = 0,
  BridgeDirectionOut   = 1,
  BridgeDirectionBoth  = 2
} BridgeDirection;



// -----------------------------------------------------------------------------
//
// BridgeDriver - the contract a bridge plugin fills in
//
// One .so per transport, exporting exactly one symbol: bridgeRegister(). The
// broker zeroes this struct, calls bridgeRegister() with it, and the plugin
// fills in what it supports.
//
// ⭐ A NULL FUNCTION POINTER MEANS "NOT SUPPORTED" - the same convention the DB
// and TRoE drivers use. The broker checks before calling and answers
// BRIDGE_UNSUPPORTED. This is what lets a v1 topic-only plugin ship against a
// contract that already names services and actions, and what lets a plugin for
// a one-directional transport simply not have a publish().
//
// ⭐ The struct is APPEND-ONLY across revisions (see BRIDGE_ABI_VERSION), which
// is what let this header name services and actions from the outset while
// implementing neither. serviceInvoke() was appended in ABI 2, once the shape
// of an invocation had been carried end to end, and serviceInvokeTracked() in
// ABI 3, once somebody had to wait for one; the action entry points stay absent
// rather than reserved on the same terms, because a goal has not been carried.
//
// ⚠⚠ APPEND-ONLY IS SAFE IN ONE DIRECTION ONLY, WHICH IS WHY abiVersion IS AN
// IN-OUT FIELD. See the handshake on it below.
//
//   an OLDER PLUGIN with a newer host - safe on its own, and it is the case the
//   policy was written for. The plugin fills in the slots it knows, the rest
//   are left at the zero the host memset them to, and NULL already means "not
//   supported".
//
//   a NEWER PLUGIN with an older host - would NOT be safe without the
//   handshake. The HOST allocates this struct, at the size its own header says,
//   and bridgeRegister() then writes whatever slots the PLUGIN's header has. A
//   plugin built against ABI 2 given an ABI 1 host would write two pointers
//   past the end of the host's struct - into the next element of the host's
//   bridges[] array, or past it - and there is no check the HOST could add
//   afterwards, because the damage is done by the time bridgeRegister returns.
//
// So the plugin has to know, before it writes anything, how much room it has.
// That is the one thing it cannot be told by a parameter - bridgeRegister's
// signature is fixed - and it is why the handshake runs through a field that
// has existed since ABI 1.
//
typedef struct BridgeDriver
{
  const char*  alias;                                 // "dds", "mqtt", "opcua"
  const char*  version;                               // the plugin's own version string


  // ---------------------------------------------------------------------------
  //
  // abiVersion - IN: the HOST's. OUT: the PLUGIN's.
  //
  // ⭐ ONE FIELD, TWO DIRECTIONS, AND IT HAS TO BE THIS FIELD. The handshake
  // needs somewhere to happen that exists in EVERY revision of this struct,
  // including the oldest one a host might have been built against - and an
  // appended field is by definition not that. abiVersion is the only member
  // whose meaning is the same question on both sides; all that differs is who
  // is answering it.
  //
  // The protocol, in full:
  //
  //   1. the host zeroes the struct and writes ITS OWN BRIDGE_ABI_VERSION here
  //   2. the host calls bridgeRegister()
  //   3. the plugin READS it, and fills in no slot the host is too old to have
  //   4. the plugin OVERWRITES it with its own, which is what the host reports
  //      in GET /version and compares against its own to log a mismatch
  //
  // ⚠ ZERO MEANS A HOST FROM BEFORE THE HANDSHAKE, not "ABI 0". Such a host
  // memset the struct and called straight in, so the safe reading of a zero is
  // 1 - the revision that existed when that was all there was.
  //
  // A plugin older than the handshake simply overwrites the field in step 3,
  // which is exactly what it did before and is why nothing had to change on
  // that side.
  //
  int          abiVersion;

  KArg*        args;                                   // plugin CLI options, spliced into the broker's arg table (NULL if none)


  // ---------------------------------------------------------------------------
  //
  // init - bring the transport up
  //
  // Called after the command line has been parsed and after the broker's own
  // subsystems are up, so that a sample arriving on the first callback has
  // somewhere to land.
  //
  // @param configFile  path to the plugin's own configuration file, or NULL.
  //                    The format belongs to the plugin - the broker neither
  //                    parses nor validates it.
  // @param brokerP     the broker's side of the seam. The plugin must keep this
  //                    pointer; it stays valid until close() returns.
  //
  // @return BRIDGE_OK, or BRIDGE_ERR with the reason already logged through
  //         brokerP->logFunction.
  //
  int (*init)(const char* configFile, const BridgeBroker* brokerP);


  // ---------------------------------------------------------------------------
  //
  // close - take the transport down
  //
  // Must not return until every plugin thread that could call sampleIn() has
  // stopped. The broker tears down the state those calls land in immediately
  // afterwards.
  //
  void (*close)(void);


  // ---------------------------------------------------------------------------
  //
  // channelAdd - start carrying an endpoint
  //
  // Called once per Channel: at startup for Channels read from configuration,
  // and again at runtime whenever one is created.
  //
  // For BridgeDirectionIn (or Both) the plugin subscribes, and from then on
  // calls brokerP->sampleIn() as data arrives. For Out it need only make sure
  // publish() will work.
  //
  // @return BRIDGE_OK, BRIDGE_BAD_INPUT if the endpoint is not well formed for
  //         this transport, or BRIDGE_UNSUPPORTED for a kind it cannot carry.
  //
  int (*channelAdd)(const char* endpoint, BridgeChannelKind kind, BridgeDirection direction);


  // ---------------------------------------------------------------------------
  //
  // channelDel - stop carrying an endpoint
  //
  // @return BRIDGE_OK, or BRIDGE_NOT_FOUND if the endpoint was never added.
  //
  int (*channelDel)(const char* endpoint);


  // ---------------------------------------------------------------------------
  //
  // publish - send a value out to an endpoint
  //
  // Called on a BROKER thread, inside the request that changed the attribute.
  // An implementation must therefore not block on anything slow - hand the
  // payload to the transport and return.
  //
  // @param json  NUL-terminated JSON text. Borrowed: the plugin must copy
  //              anything it needs after returning.
  //
  int (*publish)(const char* endpoint, const char* json);


  // ---------------------------------------------------------------------------
  //
  // versionInfo - a one-line description of the plugin and what it links
  //
  // Returns a static string, e.g. "dds 0.1.0 (Fast DDS 3.3.0, DDS Enabler
  // 1.2.0)". It is copied into the response of GET /version.
  //
  // ⭐ Unlike the API-plugin contract, which hands the plugin a KAlloc and a
  // KjNode to fill, this returns a plain string. A bridge plugin is not
  // necessarily a C program and must not be required to build a kjson tree -
  // see the note on the plain-data seam in BridgeBroker.h.
  //
  const char* (*versionInfo)(void);


  // ---------------------------------------------------------------------------
  //
  // serviceInvoke - send a request to a service endpoint, ABI 2
  //
  // ⭐ NOT publish(). A topic and a service are two different things and giving
  // them one entry point would give them one return code, one trace line and
  // one set of failure modes, which they do not share: publishing is finished
  // when the payload is on the wire, while invoking has only started there -
  // something answers, or nothing does and that is its own outcome.
  //
  // The reply is NOT returned here. It comes back later, through the broker's
  // sampleQualifiedIn(), on the same endpoint - so this call does not block a
  // broker thread on a foreign peer's latency, and a slow service cannot hold
  // an NGSI-LD request open.
  //
  // ⭐ Which means the plugin OWNS THE CORRELATION. The transport hands back
  // some request handle; matching the reply to it, and to the endpoint it
  // belongs to, happens inside the plugin. Nothing about it crosses the seam,
  // because the broker has nothing to do with it: the endpoint is what
  // identifies the attribute, and that is all the broker needs.
  //
  // That holds for as long as nobody waits for the answer. A request that does
  // (ddsSync) goes through serviceInvokeTracked() instead - see there.
  //
  // Called on a BROKER thread, inside the request that wrote the attribute, and
  // must not block - as publish().
  //
  // @param json  the request payload. Borrowed, as everywhere on this seam.
  //
  // @return BRIDGE_OK when the request is on its way, BRIDGE_NOT_FOUND when no
  //         peer serves the endpoint, BRIDGE_BAD_INPUT when the payload does
  //         not fit the service's request type.
  //
  int (*serviceInvoke)(const char* endpoint, const char* json);


  // ---------------------------------------------------------------------------
  //
  // serverIface - the peer side of this transport, ABI 2
  //
  // ⛔ THE BROKER MUST NOT CALL THIS. It is here because a bridge plugin is
  // loaded by hosts other than the broker - the functional test client is one -
  // and a request/reply transport cannot be tested without something on the
  // domain that answers. See BridgeServer.h for why that is a separate struct
  // rather than more slots here.
  //
  // @return the plugin's own static interface, or NULL when it cannot serve.
  //
  const BridgeServer* (*serverIface)(void);


  // ---------------------------------------------------------------------------
  //
  // serviceInvokeTracked - serviceInvoke(), for a request somebody waits for,
  //                        ABI 3
  //
  // The same request, sent the same way, except that its reply must come back
  // through brokerP->replyIn() carrying TOKEN - so that the NGSI-LD request
  // waiting for it (ddsSync) gets its own reply and no other.
  //
  // ⭐ THE BROKER CHOOSES THE TOKEN, and that is the whole reason this is not
  // serviceInvoke() returning one. The broker has to be waiting for the reply
  // BEFORE it can arrive, and a reply can arrive before this call returns - a
  // transport may answer on another thread at once, and one that answers
  // inline (the loopback bridge) does so before returning at all. A token the
  // plugin handed back would be known too late to wait on. So the broker
  // registers the token, then calls this, and the plugin keeps it next to its
  // own transport's request handle until the reply comes.
  //
  // The token is opaque to the plugin: never 0, unique among the requests in
  // flight, and to be handed back unchanged.
  //
  // Called on a BROKER thread and must not block - the broker does the waiting,
  // not the plugin.
  //
  // A plugin without it (NULL) is a plugin whose services cannot be waited for,
  // and the broker says so to the request that asked. serviceInvoke() stays the
  // entry point for everything else.
  //
  // @return as serviceInvoke()
  //
  int (*serviceInvokeTracked)(const char* endpoint, const char* json, uint64_t token);
} BridgeDriver;



// -----------------------------------------------------------------------------
//
// BridgeRegisterFunc - the one symbol a bridge .so exports
//
// ⚠ A plugin written in C++ must declare it extern "C", or the broker's dlsym
// for "bridgeRegister" will not find it.
//
typedef void (*BridgeRegisterFunc)(BridgeDriver* driverP);



// -----------------------------------------------------------------------------
//
// bridges / bridgeCount - the broker's registry of loaded bridge plugins
//
extern BridgeDriver  bridges[BRIDGES_MAX];
extern int           bridgeCount;

#endif  // CORBRIDGE_BRIDGEDRIVER_H_
