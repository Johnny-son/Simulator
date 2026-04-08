#pragma once

#include <cstdint>

#include "Uop.h"

struct DecodeToRename {
  bool valid = false;
  CommonUop uop{};
};

struct RenameToDispatch {
  bool valid = false;
  CommonUop uop{};

  uint16_t src_preg[CommonUop::kMaxSrcRegs] = {0, 0, 0};
  uint16_t dst_preg[CommonUop::kMaxDstRegs] = {0, 0};
  uint16_t old_dst_preg[CommonUop::kMaxDstRegs] = {0, 0};

  bool src_ready[CommonUop::kMaxSrcRegs] = {false, false, false};
  uint16_t rob_id = 0;
};

struct DispatchToIssue {
  bool valid = false;
  CommonUop uop{};

  uint16_t src_preg[CommonUop::kMaxSrcRegs] = {0, 0, 0};
  uint16_t dst_preg[CommonUop::kMaxDstRegs] = {0, 0};
  bool src_ready[CommonUop::kMaxSrcRegs] = {false, false, false};

  uint16_t rob_id = 0;
  FuClass target_fu = FuClass::None;
};

struct IssueToExecute {
  bool valid = false;
  CommonUop uop{};

  uint16_t src_preg[CommonUop::kMaxSrcRegs] = {0, 0, 0};
  uint16_t dst_preg[CommonUop::kMaxDstRegs] = {0, 0};
  uint16_t rob_id = 0;
};

struct ExecuteToCommit {
  bool valid = false;
  CommonUop uop{};

  uint16_t dst_preg[CommonUop::kMaxDstRegs] = {0, 0};
  uint64_t result[CommonUop::kMaxDstRegs] = {0, 0};
  bool dst_valid[CommonUop::kMaxDstRegs] = {false, false};

  bool exception_raised = false;
  ExceptHint exception_kind = ExceptHint::None;
  uint16_t rob_id = 0;
};
