#ifndef ONEFLOW_CORE_OPERATOR_LOG_COUNTER_OP_H_
#define ONEFLOW_CORE_OPERATOR_LOG_COUNTER_OP_H_

#include "oneflow/core/operator/operator.h"
#include "oneflow/core/graph/logical_node.h"

namespace oneflow {

class LogCounterOp final : public Operator {
 public:
  OF_DISALLOW_COPY_AND_MOVE(LogCounterOp);
  LogCounterOp() = default;
  ~LogCounterOp() = default;

  void InitFromOpConf() override;
  const PbMessage& GetCustomizedConf() const override;
  virtual LogicalNode* NewProperLogicalNode() { return new PrintLogicalNode; }
  void InferBlobDescs(std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
                      const ParallelContext* parallel_ctx) const override {}

 private:
  bool IsInputBlobAllowedModelSplit(const std::string& ibn) const override { return true; }
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_OPERATOR_LOG_COUNTER_OP_H_