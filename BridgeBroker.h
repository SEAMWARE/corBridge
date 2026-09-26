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

#include <stdbool.h>                                  // bool
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
#define BRIDGE_ABI_VERSION  8



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
// -----------------------------------------------------------------------------
//
// BridgeGoalState - where a goal sent with actionGoalSend() stands, ABI 4
//
// The seam's own vocabulary, not a transport's: a plugin maps its transport's
// codes onto these. A code that is not a goal state - a cancel request the
// server refused, say - is not mapped: the goal's state is simply unchanged,
// and the event carries the transport's message in its payload.
//
// The last five are TERMINAL. A timeout of the transport's own is FAILED.
//
typedef enum BridgeGoalState
{
  BridgeGoalUnknown    = 0,    // the plugin has not been told yet
  BridgeGoalAccepted   = 1,
  BridgeGoalExecuting  = 2,
  BridgeGoalCanceling  = 3,    // a cancel was accepted; the goal has not stopped yet
  BridgeGoalSucceeded  = 4,
  BridgeGoalCanceled   = 5,
  BridgeGoalAborted    = 6,    // the server gave up on it
  BridgeGoalRejected   = 7,    // never accepted - so no result will ever come
  BridgeGoalFailed     = 8     // the transport lost it (a timeout, a vanished server)
} BridgeGoalState;



// -----------------------------------------------------------------------------
//
// BridgeGoalPart - which part of a goal an event's payload is, ABI 5
//
// A goal reports three kinds of thing - its status, its feedback while it runs,
// and its result - and each plugin names them in its own transport's words
// ("feedback", "ddsActionFeedback", ...). The broker needs to know WHICH one a
// payload is without knowing those words, to show a goal the same way whatever
// carries it: goalFeedback, goalResult. The plugin knows; this is it saying so.
//
typedef enum BridgeGoalPart
{
  BridgeGoalPartNone      = 0,    // no payload - the state change alone
  BridgeGoalPartStatus    = 1,
  BridgeGoalPartFeedback  = 2,
  BridgeGoalPartResult    = 3
} BridgeGoalPart;

#define BRIDGE_GOAL_TERMINAL(state)  ((state) >= BridgeGoalSucceeded)



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


  // ---------------------------------------------------------------------------
  //
  // goalEventIn - something happened to a goal sent with actionGoalSend(), ABI 4
  //
  // A goal is not answered once, like a request: it is accepted or refused, it
  // runs and reports feedback, it changes state, and it ends with a result. All
  // of that comes through here, one call per event, each carrying the token the
  // broker chose when it sent the goal.
  //
  // ⭐ ONE CALL CARRIES final, AND IT IS THE LAST ONE FOR ITS TOKEN - the
  // plugin guarantees both. A transport may well deliver a goal's terminal
  // status and its result in either order, on different threads; the plugin
  // holds back whichever comes first until the other is there, and sends the
  // later of the two with final set. A goal that can have no result (Rejected)
  // is final at once. Only the plugin can know which terminal states are
  // followed by a result, because that is a rule of its transport - so the
  // buffering is there, and the broker can end a goal on final without ever
  // meeting an event for it afterwards.
  //
  // @param token        the broker's, from actionGoalSend(), handed back
  //                     unchanged
  // @param goalId       the transport's own id for the goal (a UUID, for DDS),
  //                     as text. Opaque to the broker; there for tracing and
  //                     for showing to a user.
  // @param goalAlias    a URI-shaped name the PLUGIN gives the goal - the
  //                     envelope again, as datasetId is to sampleQualifiedIn().
  //                     What the broker makes of it depends on how it models
  //                     goals, which is why the seam does not call it anything
  //                     more specific. NULL when the plugin has none.
  // @param state        the goal's BridgeGoalState as of this event
  // @param final        nothing more will come for this token
  // @param subAttrName  the envelope: the sub-attribute the payload goes in,
  //                     exactly as the plugin spells it. NULL when the event is
  //                     the state change alone and has no payload.
  // @param json         the payload (feedback, status, result), or NULL
  // @param publishTime  as sampleIn()
  //
  // An event for a token the broker does not know (a goal it already ended, or
  // one another host sent) is dropped with BRIDGE_NOT_FOUND.
  //
  // Called from a plugin thread, as sampleIn().
  //
  // ⚠ ADDED IN ABI 4. A plugin must check brokerP->abiVersion >= 4 AND that
  // this pointer is not NULL before calling it, as for replyIn().
  //
  int (*goalEventIn)(const char* bridgeName,
                     const char* endpoint,
                     uint64_t    token,
                     const char* goalId,
                     const char* goalAlias,
                     int         state,
                     bool        final,
                     const char* subAttrName,
                     const char* json,
                     int64_t     publishTime);


  // ---------------------------------------------------------------------------
  //
  // goalEventPartIn - goalEventIn, saying which PART of the goal the payload is, ABI 5
  //
  // Everything goalEventIn says, plus part (a BridgeGoalPart): whether json is
  // the goal's status, its feedback or its result. The sub-attribute stays the
  // plugin's to name; part is what lets the broker present a goal without
  // knowing that name - goalFeedback and goalResult on the goal resource.
  //
  // A plugin built against ABI 5 calls this INSTEAD of goalEventIn when the
  // host has it, and goalEventIn otherwise.
  //
  // ⚠ ADDED IN ABI 5. A plugin must check brokerP->abiVersion >= 5 AND that
  // this pointer is not NULL before calling it.
  //
  int (*goalEventPartIn)(const char* bridgeName,
                         const char* endpoint,
                         uint64_t    token,
                         const char* goalId,
                         const char* goalAlias,
                         int         state,
                         bool        final,
                         int         part,
                         const char* subAttrName,
                         const char* json,
                         int64_t     publishTime);


  // ---------------------------------------------------------------------------
  //
  // THE META OBJECT, ABI 6 - what the transport says ABOUT a payload
  //
  // Every transport carries curiosities of its own beside the payload: DDS an
  // instance handle, the publishing participant, the data type's name; MQTT a
  // retain flag, a QoS; OPC-UA a status code, a source timestamp. A client of
  // that transport may want them, and the broker cannot know what they are
  // called or mean.
  //
  // So the plugin hands them over as a JSON object - meta - and the broker
  // makes each member a Property sub-attribute, named as the plugin spelled it,
  // holding the member's value as it is:
  //
  //   meta { "instanceHandleId": "01.0f...", "ddsDataType": "Pose" }
  //   ->   "instanceHandleId": { "type": "Property", "value": "01.0f..." },
  //        "ddsDataType":      { "type": "Property", "value": "Pose" }
  //
  // on WHATEVER the payload lands in: the attribute itself for a sample, the
  // sub-attribute (subAttrName) for a reply or a goal event. The broker never
  // interprets a member - the names are quoted, not adopted, as endpoints and
  // sub-attribute names are. NULL, or an empty object: nothing is added.
  //
  // Three upcalls take it, each the one before it plus meta; a plugin calls
  // the meta form when the host has it, the plain one otherwise.
  //
  // ⚠ ADDED IN ABI 6. A plugin must check brokerP->abiVersion >= 6 AND that
  // the pointer is not NULL before calling any of the three.
  //

  // sampleMetaIn - sampleIn, plus meta
  int (*sampleMetaIn)(const char* bridgeName,
                      const char* endpoint,
                      const char* json,
                      const char* meta,
                      int64_t     publishTime);

  // replyMetaIn - replyIn, plus meta (on the sub-attribute the reply goes in)
  int (*replyMetaIn)(const char* bridgeName,
                     const char* endpoint,
                     uint64_t    token,
                     const char* datasetId,
                     const char* subAttrName,
                     const char* json,
                     const char* meta,
                     int64_t     publishTime);

  // goalEventMetaIn - goalEventPartIn, plus meta (on the sub-attribute the event goes in)
  int (*goalEventMetaIn)(const char* bridgeName,
                         const char* endpoint,
                         uint64_t    token,
                         const char* goalId,
                         const char* goalAlias,
                         int         state,
                         bool        final,
                         int         part,
                         const char* subAttrName,
                         const char* json,
                         const char* meta,
                         int64_t     publishTime);


  // ---------------------------------------------------------------------------
  //
  // replyExchangeIn - a reply, and the request it answers, ABI 7
  //
  // replyMetaIn() plus the REQUEST: what was asked, under a sub-attribute of
  // its own, written in the SAME write as the reply - one change of the
  // attribute, one notification, showing the whole exchange. A request/reply
  // transport knows things about the request the broker does not (DDS: the
  // request id, the request's data type), and a client reading the answer may
  // want to see what it answers.
  //
  // @param requestSubAttrName  the sub-attribute the request goes in ("request")
  // @param requestJson         what was sent, as JSON text
  // @param requestMeta         the transport's meta about the request (see ABI 6), or NULL
  // @param requestTime         when it was sent, nanoseconds since the epoch - 0: not said
  //
  // Everything else - the reply's sub-attribute, the token, what the broker
  // does when a request waits for it - is exactly replyMetaIn(). A NULL
  // requestSubAttrName or requestJson makes it replyMetaIn().
  //
  // ⚠ ADDED IN ABI 7. A plugin must check brokerP->abiVersion >= 7 AND that
  // this pointer is not NULL before calling it.
  //
  int (*replyExchangeIn)(const char* bridgeName,
                         const char* endpoint,
                         uint64_t    token,
                         const char* datasetId,
                         const char* requestSubAttrName,
                         const char* requestJson,
                         const char* requestMeta,
                         int64_t     requestTime,
                         const char* subAttrName,
                         const char* json,
                         const char* meta,
                         int64_t     publishTime);


  // ---------------------------------------------------------------------------
  //
  // endpointDiscoveredIn - the transport has found a service or an action nobody configured, ABI 8
  //
  // A transport that discovers what is on its bus (DDS: the Enabler announces
  // every service and action server it finds) tells the broker about the ones
  // no Channel carries. The broker may then carry it itself: a Channel of its
  // own, on the bridge's catch-all entity, with the endpoint as the attribute -
  // as Orion-LD does with the services and actions it discovers - and hands it
  // back through channelAdd() like any other. Without a catch-all the broker
  // ignores it.
  //
  // The plugin must be able to CARRY what it reports: whatever it needs to send
  // a request or a goal there (DDS: the types, as discovered) it keeps before
  // it calls this. Topics are not reported - an unclaimed sample already finds
  // the catch-all on its own.
  //
  // Called on a thread of the plugin's own, never from inside a callback of the
  // transport's that holds its locks: the broker calls back into the plugin
  // (channelAdd) before it returns.
  //
  // @param kind  a BridgeChannelKind (BridgeDriver.h): BridgeChannelService or BridgeChannelAction
  //
  // @return BRIDGE_OK when a Channel carries the endpoint now (it did already,
  //         or the broker made one), BRIDGE_NOT_FOUND when the broker left it
  //         alone (no catch-all), BRIDGE_BAD_INPUT for a kind it does not take.
  //
  // ⚠ ADDED IN ABI 8. A plugin must check brokerP->abiVersion >= 8 AND that
  // this pointer is not NULL before calling it.
  //
  int (*endpointDiscoveredIn)(const char* bridgeName,
                              const char* endpoint,
                              int         kind);
} BridgeBroker;

#endif  // CORBRIDGE_BRIDGEBROKER_H_
