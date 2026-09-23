#ifndef CORBRIDGE_BRIDGESERVER_H_
#define CORBRIDGE_BRIDGESERVER_H_

//
// FILE            BridgeServer.h
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

#include <stdint.h>                                   // uint64_t, int64_t



// -----------------------------------------------------------------------------
//
// BridgeServer - the PEER side of a bridge, which the broker never uses
//
// ⭐ THE BROKER IS A CLIENT AND ONLY A CLIENT. It invokes a service; it does not
// answer one. That is not a first-cut limitation to be lifted later: a context
// broker knows about entities, subscriptions and registrations, and has no
// extension point for the business logic that would COMPUTE a reply. Every
// NGSI-LD integration with a request/reply transport lands on that same
// boundary.
//
// But a boundary that nothing can stand on the other side of is untestable. The
// only thing on a DDS domain that would answer the broker's request is an
// application, and requiring one - a ROS 2 image, an external process - is
// exactly what keeps a test out of CI. So the test client gets to be the peer.
//
// ⭐ WHICH IS WHY THIS IS A SEPARATE STRUCT AND NOT MORE SLOTS ON BridgeDriver.
// The driver is the header the BROKER compiles against, and every entry point
// in it is one the broker may call. Appending "answer a request" to it would
// put the thing the boundary forbids inside the boundary, with only a comment
// saying not to. Here the separation is structural: a host that wants to serve
// asks for this interface by name, and the broker never asks.
//
// A plugin that cannot serve - or one built before this existed - returns NULL
// from serverIface(), which is the same "NULL means unsupported" convention the
// rest of the contract uses.
//
// ⚠ APPEND-ONLY, like the other two structs, and for the same reason: the
// PLUGIN owns this allocation (it returns a pointer to its own static), so a
// host built against a newer revision than the plugin must never read past what
// the plugin filled in. abiVersion is how it knows where that ends.
//
typedef struct BridgeServer BridgeServer;



// -----------------------------------------------------------------------------
//
// BridgeServiceRequestFunc - a peer asked us something
//
// Handed to serviceServe() rather than registered globally: the host that
// announced the service is the host that answers it, and a callback passed
// alongside the announcement cannot be left dangling by a second host.
//
// ⚠ CALLED FROM A PLUGIN THREAD, with everything that implies - see
// BridgeBroker.h's note on sampleIn().
//
// @param requestId  the transport's correlation handle. OPAQUE: the host stores
//                   it, hands it back to serviceReply(), and never interprets
//                   it. A reply is not an answer to a service - it is an answer
//                   to one request on it, and several may be in flight.
//
// @return BRIDGE_OK when the request was taken. The handler is NOT required to
//         have replied by the time it returns: an implementation may answer
//         from another thread later, which is what makes a slow peer possible.
//
typedef int (*BridgeServiceRequestFunc)(const char* bridgeName,
                                        const char* endpoint,
                                        const char* json,
                                        uint64_t    requestId,
                                        int64_t     publishTime);



struct BridgeServer
{
  int  abiVersion;                                    // BRIDGE_ABI_VERSION the PLUGIN was built with


  // ---------------------------------------------------------------------------
  //
  // serviceServe - announce a service and answer it from here on
  //
  // From the moment this returns BRIDGE_OK, the endpoint exists on the
  // transport as far as any client can tell, and every request on it reaches
  // the handler.
  //
  // ⚠ Announcing a service the transport cannot describe fails. Unlike a topic,
  // whose type is learned by listening to whoever publishes it, there is
  // nobody to learn a service's request and reply types FROM before the first
  // client arrives - so they must already be known to the plugin (see the note
  // on type discovery in the bridge documentation).
  //
  // @return BRIDGE_OK, BRIDGE_BAD_INPUT for an endpoint this transport cannot
  //         name, or BRIDGE_ERR when the transport refused.
  //
  int (*serviceServe)(const char* endpoint, BridgeServiceRequestFunc handler);


  // ---------------------------------------------------------------------------
  //
  // serviceUnserve - stop answering a service
  //
  // @return BRIDGE_OK, or BRIDGE_NOT_FOUND if the endpoint was never announced.
  //
  int (*serviceUnserve)(const char* endpoint);


  // ---------------------------------------------------------------------------
  //
  // serviceReply - answer one request
  //
  // @param requestId  exactly what the handler was given. A requestId that
  //                   matches nothing in flight is BRIDGE_NOT_FOUND rather than
  //                   a silent drop - answering twice, or answering after a
  //                   timeout, is a bug in the host and it should be told.
  //
  int (*serviceReply)(const char* endpoint, uint64_t requestId, const char* json);
};

#endif  // CORBRIDGE_BRIDGESERVER_H_
