/** @file

  This plugin counts the number of times every header has appeared.
  Maintains separate counts for client and origin headers.

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
 */

#include <iostream>
#include <map>
#include <memory>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ts/ts.h>
#include <ts/remap.h>
#include <ts/nexthop.h>
#include <tscpp/api/parentresult.h>

#include "consistenthash_config.h"
#include "strategy.h"

//extern const char PLUGIN_NAME[];
//const char PLUGIN_NAME[]   = "pparent_select";

namespace
{
// typedef std::map<std::string, std::unique_ptr<TSNextHopSelectionStrategy>, std::less<>> strategies_map;

strategies_map globalStrategies;

const char VENDOR_NAME[]   = "Apache Software Foundation";
const char SUPPORT_EMAIL[] = "dev@trafficserver.apache.org";

// maps from header name to # of times encountered
std::map<std::string, unsigned int> client_freq;
std::map<std::string, unsigned int> origin_freq;

const char CONTROL_MSG_LOG[]     = "log"; // log all data
const size_t CONTROL_MSG_LOG_LEN = sizeof(CONTROL_MSG_LOG) - 1;

// void
// Log_Data(std::ostream &ss)
// {
//   ss << std::endl << std::string(100, '+') << std::endl;

//   ss << "CLIENT HEADERS" << std::endl;
//   for (auto &elem : client_freq) {
//     ss << elem.first << ": " << elem.second << std::endl;
//   }

//   ss << std::endl;

//   ss << "ORIGIN HEADERS" << std::endl;
//   for (auto &elem : origin_freq) {
//     ss << elem.first << ": " << elem.second << std::endl;
//   }

//   ss << std::string(100, '+') << std::endl;
// }

/**
 * Logs the data collected, first the client, and then
 * the origin headers.
 */
// int
// CB_Command_Log(TSCont contp, TSEvent event, void *edata)
// {
//   std::string *command = static_cast<std::string *>(TSContDataGet(contp));
//   std::string::size_type colon_idx;

//   if (std::string::npos != (colon_idx = command->find(':'))) {
//     std::string path = command->substr(colon_idx + 1);
//     // The length of the data can include a trailing null, clip it.
//     if (path.length() > 0 && path.back() == '\0') {
//       path.pop_back();
//     }
//     if (path.length() > 0) {
//       std::ofstream out;
//       out.open(path, std::ios::out | std::ios::app);
//       if (out.is_open()) {
//         Log_Data(out);
//       } else {
//         TSError("[%s] Failed to open file '%s' for logging", PLUGIN_NAME, path.c_str());
//       }
//     } else {
//       TSError("[%s] Invalid (zero length) file name for logging", PLUGIN_NAME);
//     }
//   } else {
//     Log_Data(std::cout);
//   }

//   // cleanup.
//   delete command;
//   TSContDestroy(contp);
//   return TS_SUCCESS;
// }

/**
 * Records all headers found in the buffer in the map provided. Comparison
 * against existing entries is case-insensitive.
 */
static void
count_all_headers(TSMBuffer &bufp, TSMLoc &hdr_loc, std::map<std::string, unsigned int> &map)
{
  TSMLoc hdr, next_hdr;
  hdr           = TSMimeHdrFieldGet(bufp, hdr_loc, 0);
  int n_headers = TSMimeHdrFieldsCount(bufp, hdr_loc);
  TSDebug(PLUGIN_NAME, "%d headers found", n_headers);

  // iterate through all headers
  for (int i = 0; i < n_headers && nullptr != hdr; ++i) {
    int hdr_len;
    const char *hdr_name = TSMimeHdrFieldNameGet(bufp, hdr_loc, hdr, &hdr_len);
    std::string str      = std::string(hdr_name, hdr_len);

    // make case-insensitive by converting to lowercase
    for (auto &c : str) {
      c = tolower(c);
    }

    ++map[str];

    next_hdr = TSMimeHdrFieldNext(bufp, hdr_loc, hdr);
    TSHandleMLocRelease(bufp, hdr_loc, hdr);
    hdr = next_hdr;
  }

  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
}


/**
 * @brief Get the header value
 *
 * @param bufp request's buffer
 * @param hdrLoc request's header location
 * @param header header name
 * @param headerlen header name length
 * @param value buffer for the value
 * @param valuelen lenght of the buffer for the value
 * @return pointer to the string with the value.
 */
// char *
// getHeader(TSMBuffer bufp, TSMLoc hdrLoc, const char *header, int headerlen, char *value, int *valuelen)
// {
//   TSMLoc fieldLoc = TSMimeHdrFieldFind(bufp, hdrLoc, header, headerlen);
//   char *dst       = value;
//   while (fieldLoc) {
//     TSMLoc next = TSMimeHdrFieldNextDup(bufp, hdrLoc, fieldLoc);

//     int count = TSMimeHdrFieldValuesCount(bufp, hdrLoc, fieldLoc);
//     for (int i = 0; i < count; ++i) {
//       const char *v = nullptr;
//       int vlen      = 0;
//       v             = TSMimeHdrFieldValueStringGet(bufp, hdrLoc, fieldLoc, i, &vlen);
//       if (v == nullptr || vlen == 0) {
//         continue;
//       }
//       /* append the field content to the output buffer if enough space, plus space for ", " */
//       bool first      = (dst == value);
//       int neededSpace = ((dst - value) + vlen + (dst == value ? 0 : 2));
//       if (neededSpace < *valuelen) {
//         if (!first) {
//           memcpy(dst, ", ", 2);
//           dst += 2;
//         }
//         memcpy(dst, v, vlen);
//         dst += vlen;
//       }
//     }
//     TSHandleMLocRelease(bufp, hdrLoc, fieldLoc);
//     fieldLoc = next;
//   }

//   *valuelen = dst - value;
//   return value;
// }

int
handle_send_request(TSHttpTxn txnp, TSNextHopSelectionStrategy *strategy)
{
  // In handle_send_request, we set the response_action for what to do on connect failure.
  // We do it here, because there's no hook that fires between getting a connect failure and
  // the core SM ReadResponse func (which does stuff based on response_action).
  //
  // If the response was _not_ a connect failure, we will un-set the "connect fail" response_action
  // that we set here in handle_read_response, and set the appropriate action based on the response.

  TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_SEND_REQUEST_HDR");
  TSDebug(PLUGIN_NAME, "handle_send_request got strategy '%s'", strategy->name());

  // TODO make sure this gets the _next_ parent.
  // Do we need to mark down current one first? or pass some kind of flag to say "consider first result down"?
   TSParentResult result;
   strategy->getNextHopResult(txnp, &result);

  const bool failed = true;
  const bool nextHopExists = strategy->nextHopExists(txnp);
  const bool retry = nextHopExists;
  const bool responseIsRetryable = nextHopExists;
  TSDebug(PLUGIN_NAME, "handle_send_request setting response_action hostname '%s' port %d direct %d proxy %d", result.hostname, result.port, strategy->goDirect(), strategy->parentIsProxy());
  TSHttpTxnResponseActionSet(txnp, failed, result.hostname, result.port, retry, nextHopExists, responseIsRetryable, strategy->goDirect(), strategy->parentIsProxy());

  // TODO markNextHop on failure. How?

  // TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR getting resp");
  // if (TS_SUCCESS != TSHttpTxnServerReqGet(txnp, &resp, &resp_hdr)) {
  //   TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR failed to get resp");
  //   TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  //   return TS_SUCCESS;
  // }
  // TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR got resp");


  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

static int first_call = 2;

int
handle_read_response(TSHttpTxn txnp, TSNextHopSelectionStrategy *strategy)
{
  TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR");
  TSDebug(PLUGIN_NAME, "handle_read_response got strategy '%s'", strategy->name());

  TSMBuffer resp;
  TSMLoc resp_hdr;

  TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR getting resp");
  if (TS_SUCCESS != TSHttpTxnServerRespGet(txnp, &resp, &resp_hdr)) {
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR failed to get resp");
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_SUCCESS;
  }
  TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR got resp");

  TSHttpStatus status = TSHttpHdrStatusGet(resp, resp_hdr);
  TSDebug(PLUGIN_NAME, "handle_read_response got response code: %d", status);

//    const std::string host_hdr_name = "Host";
//    int MAX_HDR_LEN = 4096;
//    char host_hdr[MAX_HDR_LEN];
//    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR getting host");
//    getHeader(resp, resp_hdr, host_hdr_name.c_str(), host_hdr_name.size(), host_hdr, &MAX_HDR_LEN);
//    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR got host");
//    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR host '%s' len %d", host_hdr, strlen(host_hdr));
//    if (strcmp("example.com", host_hdr) == 0) {
  if (first_call == 0) {
    first_call = 1;
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR setting action");
    // test: if this was the very first call, set it as a failure and make the retry parent example.com
    TSHttpTxnResponseActionSet(txnp, false, "nbc.com", 80, true, true, true, false, false);
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR set action");
  } else if (first_call == 1) {
    first_call = 2;
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR setting action2");
    // test: if this was the very first call, set it as a failure and make the retry parent example.com
    TSHttpTxnResponseActionSet(txnp, false, "msnbc.com", 80, true, true, true, false, false);
    TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_RESPONSE_HDR set action2");
  }

  unsigned int currentRetryAttempts = 0; // TODO implement
  const bool responseIsRetryable = strategy->responseIsRetryable(currentRetryAttempts, status);

  bool failed = false; // failed is whether we can possibly retry, or whether to immediately return the failure to the user.
  const char* host = "";
  int port = 0;
  bool retry = false;
  bool nextHopExists = false;
  if (responseIsRetryable) {
    TSParentResult result;
    strategy->getNextHopResult(txnp, &result);
    host = result.hostname;
    failed = result.hostname == nullptr;
    port = result.port;
    retry = result.hostname == nullptr; // TODO should this be "true?"
    nextHopExists = result.hostname == nullptr;
  }

  TSHttpTxnResponseActionSet(txnp, failed, host, port, retry, nextHopExists, responseIsRetryable, strategy->goDirect(), strategy->parentIsProxy());
  TSDebug(PLUGIN_NAME, "handle_read_response setting response_action hostname '%s' port %d direct %d proxy %d retry %d exists %d", host, port, strategy->goDirect(), strategy->parentIsProxy(), retry, nextHopExists);

  TSDebug(PLUGIN_NAME, "handle_read_response releasing hdr");
  TSHandleMLocRelease(resp, TS_NULL_MLOC, resp_hdr);
  TSDebug(PLUGIN_NAME, "handle_read_response continuing");
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_read_request(TSHttpTxn txnp, TSNextHopSelectionStrategy *strategy)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_READ_REQUEST_HDR");
  TSDebug(PLUGIN_NAME, "handle_read_request got strategy '%s'", strategy->name());

  // get the client request so we can loop through the headers
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] could not get request headers", PLUGIN_NAME);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return -1;
  }
  count_all_headers(bufp, hdr_loc, client_freq);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_send_response(TSHttpTxn txnp, TSNextHopSelectionStrategy *strategy)
{
  TSMBuffer bufp;
  TSMLoc hdr_loc;

  TSDebug(PLUGIN_NAME, "event TS_EVENT_HTTP_SEND_RESPONSE_HDR");
  // get the response so we can loop through the headers

  if (TSHttpTxnClientRespGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] could not get response headers", PLUGIN_NAME);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_ERROR);
    return -2;
  }
  count_all_headers(bufp, hdr_loc, origin_freq);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

int
handle_os_dns(TSHttpTxn txnp, TSNextHopSelectionStrategy *strategy)
{
  TSDebug(PLUGIN_NAME, "handle_os_dns calling");

  // This is not called on the first attempt.
  // So, if we got called here, we know it's because of a parent failure.
  // So, immediately find the next parent, and set the response_action.


  TSParentResult result;
  strategy->getNextHopResult(txnp, &result);

  const bool failed = result.hostname == nullptr; // failed is whether to immediately fail and return the client a 502. In this case: whether or not we found another parent.
  const bool retry = result.hostname != nullptr;
  const bool nextHopExists = strategy->nextHopExists(txnp);
  const bool responseIsRetryable = true;
  TSDebug(PLUGIN_NAME, "handle_os_dns setting response_action hostname '%s' port %d direct %d proxy %d retry %d exists %d", result.hostname, result.port, strategy->goDirect(), strategy->parentIsProxy(), retry, nextHopExists);
  TSHttpTxnResponseActionSet(txnp, failed, result.hostname, result.port, retry, nextHopExists, responseIsRetryable, strategy->goDirect(), strategy->parentIsProxy());

  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return TS_SUCCESS;
}

// int
// handle_event_lifecycle(TSHttpTxn txnp, TSNextHopSelectionStrategy *strategy)
// {
//   TSPluginMsg *msgp = static_cast<TSPluginMsg *>(edata);
//   if (0 == strcasecmp(PLUGIN_NAME, msgp->tag)) {
//     // identify the command
//     if (msgp->data_size >= CONTROL_MSG_LOG_LEN &&
//         0 == strncasecmp(CONTROL_MSG_LOG, static_cast<const char *>(msgp->data), CONTROL_MSG_LOG_LEN)) {
//       TSDebug(PLUGIN_NAME, "Scheduled execution of '%s' command", CONTROL_MSG_LOG);
//       TSCont c = TSContCreate(CB_Command_Log, TSMutexCreate());
//       TSContDataSet(c, new std::string(static_cast<const char *>(msgp->data), msgp->data_size));
//       TSContScheduleOnPool(c, 0, TS_THREAD_POOL_TASK);
//     } else {
//       TSError("[%s] Unknown command '%.*s'", PLUGIN_NAME, static_cast<int>(msgp->data_size),
//               static_cast<const char *>(msgp->data));
//     }
//   }
//   return TS_SUCCESS;
// }

/**
 * Continuation callback. Invoked to count headers on READ_REQUEST_HDR and
 * SEND_RESPONSE_HDR hooks and to log through traffic_ctl's LIFECYCLE_MSG.
 */
int
handle_hook(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = static_cast<TSHttpTxn>(edata);
  TSNextHopSelectionStrategy *strategy = static_cast<TSNextHopSelectionStrategy *>(TSContDataGet(contp));

  TSDebug(PLUGIN_NAME, "handle_hook got strategy '%s'", strategy->name());

  switch (event) {
  case TS_EVENT_HTTP_SEND_REQUEST_HDR:
    return handle_send_request(txnp, strategy);
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    return handle_read_response(txnp, strategy);
  case TS_EVENT_HTTP_READ_REQUEST_HDR: // count client headers
    return handle_read_request(txnp, strategy);
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR: // count origin headers
    return handle_send_response(txnp, strategy);
  case TS_EVENT_HTTP_OS_DNS:
    return handle_os_dns(txnp, strategy);
  // case TS_EVENT_LIFECYCLE_MSG: // Handle external command
  //   return handle_event_lifecycle(txnp, strategy);
  default:
    return TS_ERROR; // should never happen
  }
  return TS_SUCCESS; // do nothing in any of the other states
}

} // namespace

// void
// TSPluginInit(int argc, const char *argv[])
// {
//   TSDebug(PLUGIN_NAME, "initializing plugin");

//   TSPluginRegistrationInfo info = {PLUGIN_NAME, VENDOR_NAME, SUPPORT_EMAIL};
//   if (TSPluginRegister(&info) != TS_SUCCESS) {
//     TSError("[%s] Failed to load. plugin registration failed. \n", PLUGIN_NAME);
//     return;
//   }

//   for (int i = 1; i < argc; ++i) {
//     const char* config_file_path = argv[i];
//     TSDebug(PLUGIN_NAME, "Loading parent selection strategy file %s", config_file_path);
//     auto file_strategies = createStrategiesFromFile(config_file_path);
//     if (file_strategies.size() == 0) {
//       TSError("[%s] Failed to parse configuration file %s", PLUGIN_NAME, config_file_path);
//       return;
//     }

//     TSDebug(PLUGIN_NAME, "successfully created strategies in file %s num %d", config_file_path, int(file_strategies.size()));

//     bool found_strategy = false;
//     for (auto it = file_strategies.begin(); it != file_strategies.end(); it++) {
//       TSDebug(PLUGIN_NAME, "TSPluginInit globalStrategies adding '%s'", it->first.c_str());
//       globalStrategies.emplace(it->first, std::move(it->second));
//     }
//   }

// //  TSCont main_cont = TSContCreate(main_handler, NULL);
// //  TSContDataSet(main_cont, (void *)strategies->release());

//   TSCont contp = TSContCreate(handle_hook, TSMutexCreate());
//   if (contp == nullptr) {
//     // Continuation initialization failed. Unrecoverable, report and exit.
//     TSError("[%s](%s) could not create continuation", PLUGIN_NAME, __FUNCTION__);
//     abort();
//     return;
//   }

//   // TSHttpHookAdd(TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
//   // TSHttpHookAdd(TS_HTTP_SEND_REQUEST_HDR_HOOK, contp);
//   // TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
//   // TSHttpHookAdd(TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
//   // TSHttpHookAdd(TS_HTTP_OS_DNS_HOOK, contp);
//   // TSLifecycleHookAdd(TS_LIFECYCLE_MSG_HOOK, contp);
//   TSDebug(PLUGIN_NAME, "Successfully initialized strategies %d", int(globalStrategies.size()));
// }

TSReturnCode
TSRemapInit(TSRemapInterface *api_info, char *errbuf, int errbuf_size)
{
  TSDebug(PLUGIN_NAME, "TSRemapInit called strategies %d", int(globalStrategies.size()));

  // TODO add ATS API Version check here, to bail if ATS doesn't support the version necessary for strategy plugins

  if (!api_info) {
    strncpy(errbuf, "[tsstrategy_init] - Invalid TSRemapInterface argument", errbuf_size - 1);
    return TS_ERROR;
  }

  if (api_info->tsremap_version < TSREMAP_VERSION) {
    snprintf(errbuf, errbuf_size, "[TSStrategyInit] - Incorrect API version %ld.%ld", api_info->tsremap_version >> 16,
             (api_info->tsremap_version & 0xffff));
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "Remap successfully initialized");
  return TS_SUCCESS;
}

TSReturnCode
TSRemapNewInstance(int argc, char *argv[], void **ih, char *errbuff, int errbuff_size)
{
  *ih = nullptr;

  for (int i = 0; i < argc; ++i) {
    TSDebug(PLUGIN_NAME, "TSRemapNewInstance arg %d '%s'", i, argv[i]);
  }

  if (argc < 4) {
    TSError("[%s] insufficient number of arguments, %d, expected file and strategy argument.", PLUGIN_NAME, argc);
    return TS_ERROR;
  }
  if (argc > 4) {
    TSError("[%s] too many arguments, %d, only expected file and strategy argument.", PLUGIN_NAME, argc);
    return TS_ERROR;
  }

  const char *remap_from = argv[0];
  const char *remap_to = argv[1];
  const char *config_file_path = argv[2];
  const char *strategy_name = argv[3];

  TSDebug(PLUGIN_NAME, "%s %s Loading parent selection strategy file %s for strategy %s", remap_from, remap_to, config_file_path, strategy_name);
  auto file_strategies = createStrategiesFromFile(config_file_path);
  if (file_strategies.size() == 0) {
    TSError("[%s] %s %s Failed to parse configuration file %s", PLUGIN_NAME, remap_from, remap_to, config_file_path);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "'%s' '%s' successfully created strategies in file %s num %d", remap_from, remap_to, config_file_path, int(file_strategies.size()));

  std::unique_ptr<TSNextHopSelectionStrategy> strategy;

  for (auto it = file_strategies.begin(); it != file_strategies.end(); it++) {
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance strategy file had strategy named '%s'", remap_from, remap_to, it->first.c_str());
    if (strncmp(strategy_name, it->first.c_str(), strlen(strategy_name)) != 0) {
      continue;
    }
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance using '%s'", remap_from, remap_to, it->first.c_str());
    strategy = std::move(it->second);
  }

  if (strategy.get() == nullptr) {
    TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance strategy '%s' not found in file '%s'", remap_from, remap_to, strategy_name, config_file_path);
    return TS_ERROR;
  }

  TSDebug(PLUGIN_NAME, "'%s' '%s' TSRemapNewInstance successfully loaded strategy '%s' from '%s'.", remap_from, remap_to, strategy_name, config_file_path);
  // TODO is there a better solution? We could new a pointer to the unique_ptr, but that doesn't seem helpful.
  *ih = static_cast<void *>(strategy.release());
  return TS_SUCCESS;
}

// int
// handle_host(TSCont contp, TSEvent event, void *edata) {
//   if (event == TS_EVENT_HOST_LOOKUP) {
//      const sockaddr *addr = TSHostLookupResultAddrGet(static_cast<TSHostLookupResult>(edata));
//      // TODO Add logic here.
//   }
// }

extern "C" tsapi
TSRemapStatus
TSRemapDoRemap(void *ih, TSHttpTxn txnp, TSRemapRequestInfo *rri)
{
  auto strategy = static_cast<TSNextHopSelectionStrategy*>(ih);

  TSDebug(PLUGIN_NAME, "TSRemapDoRemap got strategy '%s'", strategy->name());

  TSCont cont = TSContCreate(handle_hook, TSMutexCreate());
  TSContDataSet(cont, (void *)strategy);

  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_REQUEST_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_READ_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, cont);
  TSHttpTxnHookAdd(txnp, TS_HTTP_OS_DNS_HOOK, cont);

//  strategy->findNextHop(txnp);
//  TSParentResult result;
//  TSHttpTxnParentResultGet(txnp, &result);

 TSParentResult result;
 strategy->getNextHopResult(txnp, &result);

 if (result.hostname == nullptr) {
   // TODO this will thunder the origin. Fix to 502 instead.
   return TSREMAP_NO_REMAP;
 }

 // Make sure the host is in DNS. If it's not, we need to try another host here.
 // Otherwise, the Core PPDNSLookup will loop forever on the response_action,
 // because there are no hooks before or after PPDNSLookup on lookup failure.
  // TSCont hostCont = TSContCreate(handle_host, TSMutexCreate());
  // TSContDataSet(cont, (void *)strategy);
  // TSAction action =
  //  TSHostLookup(hostCont, result.hostname, strlen(result.hostname));

  // TSParentResult result;
  // strategy->getNextHopResult(txnp, &result);
  // if (result.hostname == nullptr) {
  //   // TODO this will effectively thunder the origin.
  //   // I think this only happens if all parents are marked down.
  //   // Should we 502 instead? Or make it an option?
  //   TSDebug(PLUGIN_NAME, "TSRemapDoRemap findNextHop returned null host, failing to remap.");
  //   return TSREMAP_NO_REMAP;
  // }

  const bool failed = false;
  const bool retry = true;
  const bool nextHopExists = true;
  const bool responseIsRetryable = true;
  TSDebug(PLUGIN_NAME, "TSRemapDoRemap setting response_action hostname '%s' port %d direct %d proxy %d", result.hostname, result.port, strategy->goDirect(), strategy->parentIsProxy());
  TSHttpTxnResponseActionSet(txnp, failed, result.hostname, result.port, retry, nextHopExists, responseIsRetryable, strategy->goDirect(), strategy->parentIsProxy());
  return TSREMAP_NO_REMAP; // TODO test DID

  // strategy->findNextHop(txnp);
  // return TSREMAP_NO_REMAP; // TODO test DID

  // result->port     = 0;
  // result->retry    = false;
  //  if (hostidx >= 0) {
//  int req_host_len;
//  const char *req_host = TSUrlHostGet(rri->requestBufp, rri->requestUrl, &req_host_len);

  // if (TSUrlHostSet(rri->requestBufp, rri->requestUrl, result.hostname, strlen(result.hostname)) != TS_SUCCESS) {
  //   TSDebug(PLUGIN_NAME, "TSRemapDoRemap TSUrlHostSet failed.");
  //   return TSREMAP_NO_REMAP;
  // }
  // TSDebug(PLUGIN_NAME, "TSRemapDoRemap host changed from [%.*s] to [%s]", req_host_len, req_host, result.hostname);
  // return TSREMAP_DID_REMAP; /* host has been modified */

//  }

  // TODO implement initial strategy parent remap

  // if (read_request(txnp, config)) {
  //   return TSREMAP_DID_REMAP_STOP;
  // } else {
  //   return TSREMAP_NO_REMAP;
  // }
//  return TSREMAP_NO_REMAP;
}

///// remap plugin setup and teardown
// extern "C" tsapi
// void
// TSRemapOSResponse(void *ih, TSHttpTxn rh, int os_response_type)
// {
// }

extern "C" tsapi
void
TSRemapDeleteInstance(void *ih)
{
  auto strategy = static_cast<TSNextHopSelectionStrategy*>(ih);
  delete strategy;
}
