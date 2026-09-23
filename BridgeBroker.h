#ifndef CORBRIDGE_BRIDGEBROKER_H_
#define CORBRIDGE_BRIDGEBROKER_H_

//
// FILE            BridgeBroker.h
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



// -----------------------------------------------------------------------------
//
// BRIDGE_ABI_VERSION - the contract revision a bridge plugin is built against
//
// A bridge plugin is an EXTERNAL shared object, quite possibly built at a
// different time and against a different checkout than the broker that loads
// it. The policy is therefore ADDITIVE: fields are only ever appended to the
// structs below, never reordered, resized or repurposed, and the broker - which
// is the side that allocates and zeroes both structs - can always read a struct
// filled in by an older plugin.
//
// So the version is not a compatibility gate in the usual sense. It is bumped
// on every addition, and the loader logs a mismatch rather than refusing: an
// older plugin leaves the new slots NULL, which is already the "unsupported"
// encoding (see BridgeDriver.h). Refusing to load would turn a working
// deployment red for a capability it never asked for.
//
#define BRIDGE_ABI_VERSION  3



// -----------------------------------------------------------------------------
//
// Return codes - what a bridge entry point answers
//
#define BRIDGE_OK            0    // done
#define BRIDGE_ERR          -1    // the transport, or the plugin, failed
#define BRIDGE_NOT_FOUND    -2    // no such endpoint / channel
#define BRIDGE_UNSUPPORTED  -3    // the plugin does not implement this
#define BRIDGE_BAD_INPUT    -4    // malformed endpoint or payload



// -----------------------------------------------------------------------------
//
// Log severities - mirror ktrace's, so the broker's implementation is a switch
//
#define BRIDGE_LOG_ERROR     0
#define BRIDGE_LOG_WARNING   1
#define BRIDGE_LOG_INFO      2
#define BRIDGE_LOG_DEBUG     3
#define BRIDGE_LOG_TRACE     4



// -----------------------------------------------------------------------------
//
// BridgeBroker - what the BROKER offers a bridge plugin
//
// The broker fills this in and hands it to the plugin's init(). It is the only
// way into the broker from a plugin; a bridge must not resolve broker symbols
// by any other means.
//
// ⭐ EVERY PARAMETER HERE IS PLAIN DATA - const char*, int64_t. No KjNode, no
// KAlloc, no NGSI-LD type crosses this line, and that is deliberate on two
// counts:
//
//   1. A bridge plugin may be written in a language that is not C. The DDS
//      bridge is C++, because the DDS Enabler is a C++ library whose API takes
//      std::string and std::shared_ptr and cannot be reached from C at all. A
//      plain-data seam is the one thing every such plugin can speak.
//
//   2. Calls arrive on the PLUGIN's OWN THREADS - a DDS reader thread, an MQTT
//      network loop - which the broker did not create and whose thread-locals it
//      has not initialised. Handing a kalloc buffer across that line would be a
//      bug the day it was written.
//
// ⭐ A BRIDGE KNOWS NOTHING ABOUT ENTITIES. It carries bytes to and from an
// endpoint; which entity attribute an endpoint corresponds to is a Channel, and
// Channels live in the broker. That is why sampleIn() takes an endpoint and not
// an entity id - and why the same is true in the outbound direction (see
// BridgeDriver.h's publish()). The seam speaks (endpoint, json, time) in both
// directions and nothing else.
//
typedef struct BridgeBroker
{
  int  abiVersion;                                    // BRIDGE_ABI_VERSION the BROKER was built with


  // ---------------------------------------------------------------------------
  //
  // sampleIn - a foreign endpoint produced a value
  //
  // The plugin calls this when data arrives on an endpoint it has subscribed
  // to. The broker resolves the endpoint to its Channel, and from there does
  // the NGSI-LD work: the attribute upsert, subscription matching and
  // notification, and the temporal event.
  //
  // @param bridgeName   the alias of the calling plugin ("dds"). Present so a
  //                     future second instance of one plugin - two DDS
  //                     participants on two domains - can be told apart.
  // @param endpoint     the transport-native name the sample arrived on, e.g.
  //                     "rt/pose". NOT percent-decoded, NOT expanded: exactly
  //                     what the wire called it, so it matches the Channel.
  // @param json         the payload, as a NUL-terminated JSON text. The broker
  //                     parses it into ITS OWN buffer before returning, so the
  //                     plugin may free or reuse this the moment the call ends.
  // @param publishTime  nanoseconds since the epoch, as the transport reports
  //                     it. 0 means "the transport did not say" and the broker
  //                     substitutes its own clock.
  //
  // ⚠ CALLED FROM A PLUGIN THREAD. The broker sets up its own per-request
  // state and takes its own locks inside this call. A plugin must NOT attempt
  // to prepare any broker state itself.
  //
  // @return BRIDGE_OK, or BRIDGE_NOT_FOUND when no Channel claims the endpoint
  //         (which is not an error - it means nobody asked for this topic).
  //
  int (*sampleIn)(const char* bridgeName,
                  const char* endpoint,
                  const char* json,
                  int64_t     publishTime);


  // ---------------------------------------------------------------------------
  //
  // logFunction - write a line to the broker's log
  //
  // The signature matches what transport libraries hand their own log sinks
  // (file/line/function/severity/message), so a plugin can forward its
  // library's logging straight through without reformatting.
  //
  // ⚠ The file/line are the PLUGIN's. A broker-side implementation must pass
  // them on rather than capture its own - see the ktrace helpers, where the
  // macro form would record the helper's own position instead.
  //
  void (*logFunction)(int         severity,           // BRIDGE_LOG_*
                      const char* fileName,
                      int         lineNo,
                      const char* funcName,
                      const char* msg);


  // ---------------------------------------------------------------------------
  //
  // sampleQualifiedIn - a foreign endpoint produced something that is not a
  //                     plain sample
  //
  // A topic delivers a value and the value is the whole of it. A request/reply
  // exchange delivers a reply, and a goal delivers feedback, a status and a
  // result - several kinds of thing, arriving over time, all belonging to the
  // one attribute the endpoint is bound to, and several of them belonging to
  // one particular exchange among many in flight.
  //
  // So this is sampleIn with two qualifiers, and it is ONE entry point rather
  // than five:
  //
  // ⭐ THE ENVELOPE IS NAMED BY THE PLUGIN, WHICH IS THE POINT. How a reply or
  // a piece of feedback appears in NGSI-LD is a convention of the transport
  // world it came from, and the next release of the API is expected to replace
  // it with a first-class concept. A broker that spelled those names itself
  // would have to be changed when that happens, and would carry one transport's
  // vocabulary in its core for as long as it did not. Here the broker knows
  // only "a sub-attribute of this name, on this instance" - which is NGSI-LD it
  // already speaks - and the convention lives entirely behind the seam, in the
  // one place that is allowed to know what transport it is.
  //
  // @param datasetId    which instance of the attribute this belongs to, or
  //                     NULL for the default instance. This is how N
  //                     simultaneous exchanges on one endpoint stay apart:
  //                     N instances of one attribute is what datasetId means.
  // @param subAttrName  the sub-attribute to put the payload in, or NULL to
  //                     make it the attribute's own value. EXACTLY as the
  //                     plugin spells it - it is quoted, not adopted, the same
  //                     way an endpoint's own name is.
  //
  // Everything else - the threading, what the broker does with the write, the
  // meaning of the return codes - is as sampleIn(), which is the degenerate
  // case of this one with both qualifiers NULL.
  //
  // ⚠ ADDED IN ABI 2, and this is the direction the additive policy does NOT
  // cover by itself: the broker allocates this struct, so a plugin built
  // against 2 and loaded by a broker built against 1 would read past the end of
  // it. A plugin must check brokerP->abiVersion before calling this, exactly as
  // the broker checks a driver's slot for NULL.
  //
  int (*sampleQualifiedIn)(const char* bridgeName,
                           const char* endpoint,
                           const char* datasetId,
                           const char* subAttrName,
                           const char* json,
                           int64_t     publishTime);


  // ---------------------------------------------------------------------------
  //
  // replyIn - the reply to a request sent with serviceInvokeTracked(), ABI 3
  //
  // sampleQualifiedIn() plus the one thing it deliberately leaves out: WHICH
  // request this answers. The token is the one the broker passed to
  // serviceInvokeTracked() - the plugin hands it back unchanged, and does not
  // interpret it.
  //
  // It exists because a request can be WAITED FOR (ddsSync): the NGSI-LD
  // request that wrote the attribute holds its response until this reply
  // arrives, and it must get its own reply and no other - not a late answer to
  // an earlier invocation of the same endpoint, and not one that belongs to a
  // request that has already given up. Only a token can tell those apart; the
  // endpoint cannot.
  //
  // What the broker does with it:
  //   - a request is waiting on the token  -> the reply is handed to it, and
  //                                           written with the request's own
  //                                           write, not here
  //   - the request gave up (timed out)    -> the reply is dropped: that request
  //                                           answered with an error and wrote
  //                                           nothing, and must stay that way
  //   - anything else                      -> exactly as sampleQualifiedIn()
  //
  // So a plugin may send EVERY reply through here, tracked or not (token 0 for
  // an untracked one), once the host has it.
  //
  // ⚠ ADDED IN ABI 3. A plugin must check brokerP->abiVersion >= 3 AND that
  // this pointer is not NULL before calling it - a host that is not the broker
  // (the functional test client) is built against this header too, and fills
  // in only what it needs. Otherwise it uses sampleQualifiedIn(), as in ABI 2.
  //
  int (*replyIn)(const char* bridgeName,
                 const char* endpoint,
                 uint64_t    token,
                 const char* datasetId,
                 const char* subAttrName,
                 const char* json,
                 int64_t     publishTime);
} BridgeBroker;

#endif  // CORBRIDGE_BRIDGEBROKER_H_
