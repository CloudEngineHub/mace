// Copyright 2018 The MACE Authors. All Rights Reserved.
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

#include <functional>
#include <memory>

#include "mace/core/ops/operator.h"
#include "mace/core/registry/ops_registry.h"

namespace mace {
namespace ops {

template <DeviceType D, typename T>
class SliceOp;

template <typename T>
class SliceOp<DeviceType::CPU, T> : public Operation {
 public:
  explicit SliceOp(OpConstructContext *context)
      : Operation(context),
        axes_(Operation::GetRepeatedArgs<int>("axes")),
        starts_(Operation::GetRepeatedArgs<int>("starts")),
        ends_(Operation::GetRepeatedArgs<int>("ends")) {}

  MaceStatus Run(OpContext *context) override {
    MACE_UNUSED(context);
    const Tensor *input = this->Input(0);
    Tensor *output = this->Output(0);

    const index_t rank = input->dim_size();
    MACE_CHECK(rank >= 1, "The input dim size should >= 1.");

    index_t start = 0;
    index_t end = 0;
    index_t axis = 0;
    if (this->InputSize() > 1) {
      MACE_CHECK(this->InputSize() >= 4, "The starts/ends/axes are required.");
      const Tensor *starts = this->Input(1);
      const Tensor *ends = this->Input(2);
      const Tensor *axes = this->Input(3);
      Tensor::MappingGuard starts_guard(starts);
      Tensor::MappingGuard ends_guard(ends);
      Tensor::MappingGuard axes_guard(axes);
      const int32_t *starts_data = starts->data<int32_t>();
      const int32_t *ends_data = ends->data<int32_t>();
      const int32_t *axes_data = axes->data<int32_t>();
      MACE_CHECK(starts->size() == 1 && ends->size() == 1 && axes->size() == 1,
                 "Only support slicing at one axis.");
      start = starts_data[0];
      end = ends_data[0];
      axis = axes_data[0];
      if (this->InputSize() == 5) {
        const Tensor *steps = this->Input(4);
        Tensor::MappingGuard steps_guard(steps);
        MACE_CHECK(steps->size() == 1 && steps->data<int32_t>()[0] == 1,
                   "Only support slicing with steps 1.");
      }
    } else {
      MACE_CHECK(starts_.size() == 1 && ends_.size() == 1 && axes_.size() == 1,
                 "only support slicing at one axis.");
      start = starts_[0];
      end = ends_[0];
      axis = axes_[0];
    }
    if (axis < 0) axis += input->dim_size();
    MACE_CHECK(axis >= 0 && axis < input->dim_size(),
               "The axes are out of bounds.");
    index_t input_dim = input->dim(axis);
    if (start < 0) start += input_dim;
    if (end < 0) end += input_dim;
    MACE_CHECK(
        start < input_dim && start >= 0 && end > start && end <= input_dim,
        "The starts and ends are out of bounds.");
    index_t output_dim = end - start;
    MACE_CHECK(output_dim > 0, "output_dim should > 0");
    std::vector<index_t> output_shape = input->shape();
    output_shape[axis] = output_dim;
    MACE_RETURN_IF_ERROR(output->Resize(output_shape));

    Tensor::MappingGuard input_guard(input);
    Tensor::MappingGuard output_guard(output);
    const T *input_data = input->data<T>();
    T *output_data = output->mutable_data<T>();
    index_t inner_size = 1;
    for (index_t i = axis + 1; i < input->dim_size(); ++i) {
      inner_size *= input->dim(i);
    }
    index_t offset = start * inner_size;
    index_t input_stride = input_dim * inner_size;
    index_t output_stride = output_dim * inner_size;
    index_t outer_size = 1;
    for (index_t i = 0; i < axis; ++i) {
      outer_size *= input->dim(i);
    }
    for (index_t i = 0; i < outer_size; ++i) {
      const T *input_base = input_data + i * input_stride + offset;
      T *output_base = output_data + i * output_stride;
      memcpy(output_base, input_base, output_stride * sizeof(T));
    }

    return MaceStatus::MACE_SUCCESS;
  }

 private:
  std::vector<int> axes_;
  std::vector<int> starts_;
  std::vector<int> ends_;
};

void RegisterSlice(OpRegistry *op_registry) {
  MACE_REGISTER_OP(op_registry, "Slice", SliceOp, DeviceType::CPU, float);
  MACE_REGISTER_BF16_OP(op_registry, "Slice", SliceOp, DeviceType::CPU);
}

}  // namespace ops
}  // namespace mace
