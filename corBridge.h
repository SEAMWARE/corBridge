#ifndef CORBRIDGE_H_
#define CORBRIDGE_H_

//
// FILE            corBridge.h
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



// -----------------------------------------------------------------------------
//
// corBridgeVersion - return the library version string
//
extern const char* corBridgeVersion(void);



// -----------------------------------------------------------------------------
//
// corBridgeAbiVersion - return the contract revision this build was made with
//
// The same value as BRIDGE_ABI_VERSION in the BridgeBroker.h this library was
// compiled with. The broker does not call it today: it compares its own
// BRIDGE_ABI_VERSION with each plugin's BridgeDriver.abiVersion at load time and
// logs a mismatch (INFO). GET /version shows each loaded bridge's versionInfo()
// string only - neither the broker's bridge ABI nor a plugin's abiVersion.
//
extern int corBridgeAbiVersion(void);



// -----------------------------------------------------------------------------
//
// corBridgeKindName / corBridgeDirectionName - enum to string, for logs and
// for the stored representation of a Channel
//
// Return "invalid" for a value outside the enum rather than NULL, so a caller
// building a log line never has to check.
//
extern const char* corBridgeKindName(int kind);
extern const char* corBridgeDirectionName(int direction);



// -----------------------------------------------------------------------------
//
// corBridgeKindFromName / corBridgeDirectionFromName - string to enum
//
// Return -1 when the string names nothing, which is how a bad value in a
// configuration file or a POST body is detected.
//
extern int corBridgeKindFromName(const char* name);
extern int corBridgeDirectionFromName(const char* name);

#endif  // CORBRIDGE_H_
