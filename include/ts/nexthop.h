/** @file

  Traffic Server SDK API header file

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  @section developers Developers

  NextHop plugin interface.

 */

#pragma once

#include <ts/apidefs.h>

struct TSParentResult;

// plugin callback commands.
enum NHCmd { NH_MARK_UP, NH_MARK_DOWN };

struct NHHealthStatus {
  virtual bool isNextHopAvailable(TSHttpTxn txn, const char *hostname, const int port, void *ih = nullptr) = 0;
  virtual void markNextHop(TSHttpTxn txn, const char *hostname, const int port, const NHCmd status, void *ih = nullptr,
                           const time_t now = 0)                                                           = 0;
  virtual ~NHHealthStatus() {}
};

class TSNextHopSelectionStrategy
{
public:
  TSNextHopSelectionStrategy(){};
  virtual ~TSNextHopSelectionStrategy(){};

  virtual const char* name()                                                                                               = 0;
  virtual void getNextHopResult(TSHttpTxn txnp, TSParentResult *result, time_t now = 0) = 0;
  virtual void findNextHop(TSHttpTxn txnp, time_t now = 0)                                                                 = 0;
  virtual void markNextHop(TSHttpTxn txnp, const char *hostname, const int port, const NHCmd status, const time_t now = 0) = 0;
  virtual bool nextHopExists(TSHttpTxn txnp)                                                                               = 0;
  virtual bool responseIsRetryable(unsigned int current_retry_attempts, TSHttpStatus response_code)                        = 0;
  virtual bool onFailureMarkParentDown(TSHttpStatus response_code)                                                         = 0;

  virtual bool goDirect()      = 0;
  virtual bool parentIsProxy() = 0;
};

// ResponseNextAction is used by HandleResponse plugins.
// Plugins will set this to indicate how to retry.
//
// If handled is false, then no plugin set it, and Core will proceed to do its own thing.
//
// If handled is true, core will not do any parent processing, markdown, or anything else,
// but will use the values in this for whether to use the existing response or make another request,
// and what that request should look like.
struct TSResponseAction {
  bool handled = false;

  // TODO this shouldn't be necessary - plugins should manipulate the response as they see fit,
  // core shouldn't "know" if it was a "success" or "failure," only the response or retry data/action.
  // But for now, core needs to know, for reasons.
  bool fail = false;

  const char *hostname = nullptr;
  int port = 0;
  bool retry = false;

  bool nextHopExists = false;
  bool responseIsRetryable = false;
  bool goDirect = false;
  bool parentIsProxy = false;
};
