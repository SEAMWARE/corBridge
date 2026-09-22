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

#include <stdint.h>                                   // int64_t

#include "kargs/KArg.h"                               // KArg

#include "corBridge/BridgeBroker.h"                   // BridgeBroker, BRIDGE_*



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
// ⭐ The struct is APPEND-ONLY across revisions (see BRIDGE_ABI_VERSION). The
// service and action entry points are deliberately absent rather than reserved:
// appending them when their semantics are settled is safe precisely because the
// broker owns the allocation, and inventing their signatures now - before a
// single goal has been carried end to end - would be guessing in a header that
// external plugins compile against.
//
typedef struct BridgeDriver
{
  const char*  alias;                                 // "dds", "mqtt", "opcua"
  const char*  version;                               // the plugin's own version string
  int          abiVersion;                            // BRIDGE_ABI_VERSION the PLUGIN was built with
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
