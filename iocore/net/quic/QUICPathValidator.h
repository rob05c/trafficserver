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

#include <vector>
#include "QUICTypes.h"
#include "QUICFrameHandler.h"
#include "QUICFrameGenerator.h"

class QUICPathValidator : public QUICFrameHandler, public QUICFrameGenerator
{
public:
  QUICPathValidator() {}
  bool is_validated();
  void validate();

  // QUICFrameHandler
  std::vector<QUICFrameType> interests() override;
  QUICErrorUPtr handle_frame(std::shared_ptr<const QUICFrame> frame) override;

  // QUICFrameGeneratro
  bool will_generate_frame() override;
  QUICFrameUPtr generate_frame(uint64_t connection_credit, uint16_t maximum_frame_size) override;

private:
  enum class ValidationState : int {
    NOT_VALIDATED,
    VALIDATING,
    VALIDATED,
  };
  ValidationState _state      = ValidationState::NOT_VALIDATED;
  int _has_outgoing_challenge = 0;
  bool _has_outgoing_response = false;
  uint8_t _incoming_challenge[QUICPathChallengeFrame::DATA_LEN];
  uint8_t _outgoing_challenge[QUICPathChallengeFrame::DATA_LEN * 3];

  void _generate_challenge();
  void _generate_response(std::shared_ptr<const QUICPathChallengeFrame> frame);
  QUICErrorUPtr _validate_response(std::shared_ptr<const QUICPathResponseFrame> frame);
};
