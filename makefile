#
# FILE            makefile
#
# AUTHOR          Ken Zangelin
#
# Copyright 2026 Seamware
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
LIB_SO        = libcorBridge.so
LIB           = libcorBridge.a
CC            = gcc
INCLUDE       = -I..
DFLAGS        =
CFLAGS        = -O2 -Wall -Werror -fPIC -fstack-protector-all $(DFLAGS) $(INCLUDE) -MMD -MP
LIB_SOURCES   = corBridge.c
LIB_OBJS      = $(LIB_SOURCES:c=o)
LIB_DEPS      = $(LIB_SOURCES:c=d)

#
# Nothing to link. corBridge is the CONTRACT a bridge plugin satisfies: two
# headers of struct and enum, and a handful of enum-to-string helpers whose only
# call is strcmp. KArg.h is included for the type of BridgeDriver's args member,
# which is a pointer - no kargs code is reached, so no kargs library is linked.
#
SO_LDFLAGS    =
SO_LIBS       =
SO_RPATH      =

LIBS          =

all: $(LIB_SO) $(LIB)

clean:
						rm -f *.o
						rm -f *.a
						rm -f *~
						rm -f *.so

install:    all

di:         install

ci:         clean install

$(LIB):			$(LIB_OBJS) $(LIB_SOURCES)
						ar r $(LIB) $(LIB_OBJS)
						ranlib $(LIB)

$(LIB_SO):	$(LIB_OBJS) $(LIB_SOURCES)
						$(CC) -shared $(LIB_OBJS) -o $(LIB_SO) $(SO_LDFLAGS) $(SO_LIBS) $(SO_RPATH)

%.o: %.c
						$(CC) $(CFLAGS) -c $< -o $@

%.i: %.c
						$(CC) $(CFLAGS) -c $^ -E > $@

-include $(LIB_DEPS)
