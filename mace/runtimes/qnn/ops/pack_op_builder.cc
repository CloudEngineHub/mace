// Copyright 2022 The MACE Authors. All Rights Reserved.
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

#include "mace/runtimes/qnn/op_builder.h"

#include "mace/core/proto/arg_helper.h"

namespace mace {
class PackOpBuilder : public OpBuilder {
 public:
  explicit PackOpBuilder(GraphBuilder *graph_builder)
      : OpBuilder(graph_builder) {}

  MaceStatus BuildOp(const OperatorDef &op, DataType quantized_type) {
    MACE_UNUSED(quantized_type);
    SetOpType(QNN_OP_PACK);
    SetOpName(op.name().c_str());

    const int input_dims = graph_builder_->GetTensorShape(op.input(0)).size();
    const int output_dims = graph_builder_->GetTensorShape(op.output(0)).size();
    MACE_CHECK(output_dims == input_dims + 1);
    int axis = ProtoArgHelper::GetOptionalArg<OperatorDef, int>(op, "axis", 3);
    axis = axis < 0 ? axis + input_dims + 1 : axis;
    MACE_CHECK((0 <= axis && axis <= input_dims),
               "Expected Packing axis in the range [", -input_dims - 1, ", ",
               input_dims + 1, "], but got ", axis);
    AddScalarParam(
        QNN_OP_UN_PACK_PARAM_AXIS,
        {QNN_DATATYPE_UINT_32, .uint32Value = static_cast<uint32_t>(axis)});

    MACE_CHECK(op.input_size() >= 1);
    for (int i = 0; i < op.input_size(); ++i) {
      AddInput(op.input(i));
    }
    AddOutput(op.output(0));

    return MaceStatus::MACE_SUCCESS;
  }
};
namespace qnn {
void RegisterPack(OpRegistry *op_registry) {
  QNN_REGISTER_OP(op_registry, "Pack", PackOpBuilder);
  QNN_REGISTER_OP(op_registry, "Stack", PackOpBuilder);
}
}  // namespace qnn
}  // namespace mace
