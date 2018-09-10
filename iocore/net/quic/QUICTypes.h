/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#pragma once

#include <cstring>
#include "ts/ink_endian.h"

#include <memory>
#include <random>
#include <cstdint>
#include "ts/INK_MD5.h"
#include "ts/ink_memory.h"
#include "ts/ink_inet.h"

// These magical defines should be removed when we implement seriously
#define MAGIC_NUMBER_0 0
#define MAGIC_NUMBER_1 1
#define MAGIC_NUMBER_TRUE true

using QUICPacketNumber = uint64_t;
using QUICVersion      = uint32_t;
using QUICStreamId     = uint64_t;
using QUICOffset       = uint64_t;

// TODO: Update version number
// Note: Prefix for drafts (0xff000000) + draft number
// Note: Fix "Supported Version" field in test case of QUICPacketFactory_Create_VersionNegotiationPacket
// Note: Fix QUIC_ALPN_PROTO_LIST in QUICConfig.cc
// Note: Change ExtensionType (QUICTransportParametersHandler::TRANSPORT_PARAMETER_ID) if it's changed
constexpr QUICVersion QUIC_SUPPORTED_VERSIONS[] = {
  0xff00000e,
};
constexpr QUICVersion QUIC_EXERCISE_VERSIONS = 0x1a2a3a4a;

enum class QUICEncryptionLevel {
  NONE      = -1,
  INITIAL   = 0,
  ZERO_RTT  = 1,
  HANDSHAKE = 2,
  ONE_RTT   = 3,
};

// For range-based for loop. This starts from INITIAL to ONE_RTT. It doesn't include NONE.
// Defining begin, end, operator*, operator++ doen't work for duplicate symbol issue with libmgmt_p.a :(
// TODO: support ZERO_RTT
constexpr QUICEncryptionLevel QUIC_ENCRYPTION_LEVELS[] = {
  QUICEncryptionLevel::INITIAL,
  QUICEncryptionLevel::ZERO_RTT,
  QUICEncryptionLevel::HANDSHAKE,
  QUICEncryptionLevel::ONE_RTT,
};

// 0-RTT and 1-RTT use same Packet Number Space
constexpr QUICEncryptionLevel QUIC_PN_SPACES[] = {
  QUICEncryptionLevel::INITIAL,
  QUICEncryptionLevel::ZERO_RTT,
  QUICEncryptionLevel::HANDSHAKE,
};

// Devide to QUICPacketType and QUICPacketLongHeaderType ?
enum class QUICPacketType : uint8_t {
  VERSION_NEGOTIATION = 0,
  PROTECTED,                 // Not on the spec. but just for convenience // should be short header
  STATELESS_RESET,           // Not on the spec. but just for convenience
  INITIAL            = 0x7F, // draft-08 version-specific type
  RETRY              = 0x7E, // draft-08 version-specific type
  HANDSHAKE          = 0x7D, // draft-08 version-specific type
  ZERO_RTT_PROTECTED = 0x7C, // draft-08 version-specific type
  UNINITIALIZED      = 0xFF, // Not on the spec. but just for convenience
};

// XXX If you add or remove QUICFrameType, you might also need to change QUICFrame::type(const uint8_t *)
enum class QUICFrameType : uint8_t {
  PADDING = 0x00,
  RST_STREAM,
  CONNECTION_CLOSE,
  APPLICATION_CLOSE,
  MAX_DATA,
  MAX_STREAM_DATA,
  MAX_STREAM_ID,
  PING,
  BLOCKED,
  STREAM_BLOCKED,
  STREAM_ID_BLOCKED,
  NEW_CONNECTION_ID,
  STOP_SENDING,
  ACK,
  PATH_CHALLENGE,
  PATH_RESPONSE,
  STREAM = 0x10, // 0x10 - 0x17
  CRYPTO = 0x18,
  NEW_TOKEN,
  UNKNOWN,
};

enum class QUICVersionNegotiationStatus {
  NOT_NEGOTIATED, // Haven't negotiated yet
  NEGOTIATED,     // Negotiated
  VALIDATED,      // Validated with a one in transport parameters
  FAILED,         // Negotiation failed
};

enum class QUICKeyPhase : int {
  PHASE_0 = 0,
  PHASE_1,
  INITIAL,
  ZERO_RTT,
  HANDSHAKE,
};

enum class QUICPacketCreationResult {
  SUCCESS,
  FAILED,
  NO_PACKET,
  NOT_READY,
  IGNORED,
  UNSUPPORTED,
};

enum class QUICErrorClass {
  NONE,
  TRANSPORT,
  APPLICATION,
};

enum class QUICTransErrorCode : uint16_t {
  NO_ERROR = 0x00,
  INTERNAL_ERROR,
  SERVER_BUSY,
  FLOW_CONTROL_ERROR,
  STREAM_ID_ERROR,
  STREAM_STATE_ERROR,
  FINAL_OFFSET_ERROR,
  FRAME_ENCODING_ERROR,
  TRANSPORT_PARAMETER_ERROR,
  VERSION_NEGOTIATION_ERROR,
  PROTOCOL_VIOLATION,
  UNSOLICITED_PATH_RESPONSE = 0x0B,
  INVALID_MIGRATION         = 0x0C,
  CRYPTO_ERROR              = 0x0100, // 0x100 - 0x1FF
};

// Application Protocol Error Codes defined in application
using QUICAppErrorCode                          = uint16_t;
constexpr uint16_t QUIC_APP_ERROR_CODE_STOPPING = 0;

class QUICError
{
public:
  virtual ~QUICError() {}

  QUICErrorClass cls = QUICErrorClass::NONE;
  uint16_t code      = 0;
  const char *msg    = nullptr;

protected:
  QUICError(){};
  QUICError(QUICErrorClass error_class, uint16_t error_code, const char *error_msg = nullptr)
    : cls(error_class), code(error_code), msg(error_msg)
  {
  }
};

class QUICNoError : public QUICError
{
public:
  QUICNoError() : QUICError() {}
};

class QUICConnectionError : public QUICError
{
public:
  QUICConnectionError() : QUICError() {}
  QUICConnectionError(QUICTransErrorCode error_code, const char *error_msg = nullptr,
                      QUICFrameType frame_type = QUICFrameType::UNKNOWN)
    : QUICError(QUICErrorClass::TRANSPORT, static_cast<uint16_t>(error_code), error_msg), _frame_type(frame_type){};
  QUICConnectionError(QUICErrorClass error_class, uint16_t error_code, const char *error_msg = nullptr,
                      QUICFrameType frame_type = QUICFrameType::UNKNOWN)
    : QUICError(error_class, error_code, error_msg), _frame_type(frame_type){};

  QUICFrameType frame_type() const;

private:
  QUICFrameType _frame_type = QUICFrameType::UNKNOWN;
};

class QUICStream;

class QUICStreamError : public QUICError
{
public:
  QUICStreamError() : QUICError() {}
  QUICStreamError(const QUICStream *s, const QUICTransErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(QUICErrorClass::TRANSPORT, static_cast<uint16_t>(error_code), error_msg), stream(s){};
  QUICStreamError(const QUICStream *s, const QUICAppErrorCode error_code, const char *error_msg = nullptr)
    : QUICError(QUICErrorClass::APPLICATION, static_cast<uint16_t>(error_code), error_msg), stream(s){};

  const QUICStream *stream;
};

using QUICErrorUPtr           = std::unique_ptr<QUICError>;
using QUICConnectionErrorUPtr = std::unique_ptr<QUICConnectionError>;
using QUICStreamErrorUPtr     = std::unique_ptr<QUICStreamError>;

class QUICConnectionId
{
public:
  static const int MAX_LENGTH            = 18;
  static const size_t MAX_HEX_STR_LENGTH = MAX_LENGTH * 2 + 1;
  static QUICConnectionId ZERO();
  QUICConnectionId();
  QUICConnectionId(const uint8_t *buf, uint8_t len);

  explicit operator bool() const { return true; }
  /**
   * Note that this returns a kind of hash code so we can use a ConnectionId as a key for a hashtable.
   */
  operator uint64_t() const { return this->_hashcode(); }
  operator const uint8_t *() const { return this->_id; }
  bool
  operator==(const QUICConnectionId &x) const
  {
    if (this->_len != x._len) {
      return false;
    }
    return memcmp(this->_id, x._id, this->_len) == 0;
  }

  bool
  operator!=(const QUICConnectionId &x) const
  {
    if (this->_len != x._len) {
      return true;
    }
    return memcmp(this->_id, x._id, this->_len) != 0;
  }

  /*
   * This is just for debugging.
   */
  uint32_t h32() const;
  int hex(char *buf, size_t len) const;

  uint8_t length() const;
  bool is_zero() const;
  void randomize();

private:
  uint64_t _hashcode() const;
  uint8_t _id[MAX_LENGTH];
  uint8_t _len = 0;
};

class QUICStatelessResetToken
{
public:
  constexpr static int8_t LEN = 16;

  QUICStatelessResetToken() {}
  QUICStatelessResetToken(const uint8_t *buf) { memcpy(this->_token, buf, QUICStatelessResetToken::LEN); }
  void
  generate(QUICConnectionId conn_id, uint32_t server_id)
  {
    this->_gen_token(conn_id ^ server_id);
  }

  const uint8_t *
  buf() const
  {
    return _token;
  }

private:
  uint8_t _token[16] = {0};

  void _gen_token(uint64_t data);
};

class QUICPreferredAddress
{
public:
  constexpr static int16_t MIN_LEN = 26;
  constexpr static int16_t MAX_LEN = 295;

  QUICPreferredAddress() {}
};

enum class QUICStreamType : uint8_t {
  CLIENT_BIDI = 0x00,
  SERVER_BIDI,
  CLIENT_UNI,
  SERVER_UNI,
};

class QUICFiveTuple
{
public:
  QUICFiveTuple(){};
  QUICFiveTuple(IpEndpoint src, IpEndpoint dst, int protocol);
  void update(IpEndpoint src, IpEndpoint dst, int protocol);
  IpEndpoint source() const;
  IpEndpoint destination() const;
  int protocol() const;

private:
  IpEndpoint _source;
  IpEndpoint _destination;
  int _protocol;
  uint64_t _hash_code = 0;
};

// TODO: move version independent functions to QUICInvariants
class QUICTypeUtil
{
public:
  static bool is_supported_version(QUICVersion version);
  static QUICStreamType detect_stream_type(QUICStreamId id);
  static QUICEncryptionLevel encryption_level(QUICPacketType type);
  static QUICPacketType packet_type(QUICEncryptionLevel level);
  static QUICKeyPhase key_phase(QUICPacketType type);
  static int pn_space_index(QUICEncryptionLevel level);

  static QUICConnectionId read_QUICConnectionId(const uint8_t *buf, uint8_t n);
  static int read_QUICPacketNumberLen(const uint8_t *buf);
  static QUICPacketNumber read_QUICPacketNumber(const uint8_t *buf);
  static QUICVersion read_QUICVersion(const uint8_t *buf);
  static QUICStreamId read_QUICStreamId(const uint8_t *buf);
  static QUICOffset read_QUICOffset(const uint8_t *buf);
  static uint16_t read_QUICTransErrorCode(const uint8_t *buf);
  static QUICAppErrorCode read_QUICAppErrorCode(const uint8_t *buf);
  static uint64_t read_QUICMaxData(const uint8_t *buf);

  static void write_QUICConnectionId(QUICConnectionId connection_id, uint8_t *buf, size_t *len);
  static void write_QUICPacketNumber(QUICPacketNumber packet_number, uint8_t n, uint8_t *buf, size_t *len);
  static void write_QUICVersion(QUICVersion version, uint8_t *buf, size_t *len);
  static void write_QUICStreamId(QUICStreamId stream_id, uint8_t *buf, size_t *len);
  static void write_QUICOffset(QUICOffset offset, uint8_t *buf, size_t *len);
  static void write_QUICTransErrorCode(uint16_t error_code, uint8_t *buf, size_t *len);
  static void write_QUICAppErrorCode(QUICAppErrorCode error_code, uint8_t *buf, size_t *len);
  static void write_QUICMaxData(uint64_t max_data, uint8_t *buf, size_t *len);

private:
};

class QUICInvariants
{
public:
  static bool is_long_header(const uint8_t *buf);
  static bool is_version_negotiation(QUICVersion v);
  static bool version(QUICVersion &dst, const uint8_t *buf, uint64_t buf_len);
  /**
   * This function returns the raw value. You'll need to add 3 to the returned value to get the actual connection id length.
   */
  static bool dcil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len);
  /**
   * This function returns the raw value. You'll need to add 3 to the returned value to get the actual connection id length.
   */
  static bool scil(uint8_t &dst, const uint8_t *buf, uint64_t buf_len);
  static bool dcid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len);
  static bool scid(QUICConnectionId &dst, const uint8_t *buf, uint64_t buf_len);

  static const size_t CIL_BASE          = 3;
  static const size_t LH_VERSION_OFFSET = 1;
  static const size_t LH_CIL_OFFSET     = 5;
  static const size_t LH_DCID_OFFSET    = 6;
  static const size_t SH_DCID_OFFSET    = 1;
  static const size_t LH_MIN_LEN        = 6;
  static const size_t SH_MIN_LEN        = 1;
};