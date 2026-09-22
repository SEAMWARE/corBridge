//
// FILE            corBridge.c
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

#include <string.h>                                   // strcmp

#include "corBridge/BridgeDriver.h"                   // BridgeChannelKind, BridgeDirection
#include "corBridge/corBridge.h"                      // Own interface
#include "corBridge/version.h"                        // CORBRIDGE_VERSION



// -----------------------------------------------------------------------------
//
// corBridgeVersion -
//
const char* corBridgeVersion(void)
{
  return CORBRIDGE_VERSION;
}



// -----------------------------------------------------------------------------
//
// corBridgeAbiVersion -
//
int corBridgeAbiVersion(void)
{
  return BRIDGE_ABI_VERSION;
}



// -----------------------------------------------------------------------------
//
// corBridgeKindName -
//
const char* corBridgeKindName(int kind)
{
  switch (kind)
  {
  case BridgeChannelTopic:    return "topic";
  case BridgeChannelService:  return "service";
  case BridgeChannelAction:   return "action";
  }

  return "invalid";
}



// -----------------------------------------------------------------------------
//
// corBridgeDirectionName -
//
const char* corBridgeDirectionName(int direction)
{
  switch (direction)
  {
  case BridgeDirectionIn:    return "in";
  case BridgeDirectionOut:   return "out";
  case BridgeDirectionBoth:  return "both";
  }

  return "invalid";
}



// -----------------------------------------------------------------------------
//
// corBridgeKindFromName -
//
int corBridgeKindFromName(const char* name)
{
  if (name == NULL)
    return -1;

  if (strcmp(name, "topic")   == 0)  return BridgeChannelTopic;
  if (strcmp(name, "service") == 0)  return BridgeChannelService;
  if (strcmp(name, "action")  == 0)  return BridgeChannelAction;

  return -1;
}



// -----------------------------------------------------------------------------
//
// corBridgeDirectionFromName -
//
int corBridgeDirectionFromName(const char* name)
{
  if (name == NULL)
    return -1;

  if (strcmp(name, "in")   == 0)  return BridgeDirectionIn;
  if (strcmp(name, "out")  == 0)  return BridgeDirectionOut;
  if (strcmp(name, "both") == 0)  return BridgeDirectionBoth;

  return -1;
}
