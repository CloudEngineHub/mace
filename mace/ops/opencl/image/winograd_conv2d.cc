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

#include "mace/core/ops/op_context.h"
#include "mace/runtimes/opencl/opencl_runtime.h"
#include "mace/ops/common/activation_type.h"
#include "mace/ops/common/conv_pool_2d_util.h"
#include "mace/ops/common/utils.h"
#include "mace/runtimes/opencl/core/opencl_helper.h"
#include "mace/utils/memory.h"
#include "mace/utils/math.h"

namespace mace {
namespace ops {
namespace opencl {
namespace image {

namespace {
MaceStatus WinogradInputTransform(OpContext *context,
                                  cl::Kernel *kernel,
                                  const Tensor *input_tensor,
                                  const int *paddings,
                                  const index_t round_h,
                                  const index_t round_w,
                                  const int wino_blk_size,
                                  const bool input_changed,
                                  Tensor *output_tensor,
                                  uint32_t *kwg_size,
                                  StatsFuture *future) {
  OpenclRuntime *opencl_runtime =
      static_cast<OpenclRuntime *>(context->runtime());
  auto *executor = opencl_runtime->GetOpenclExecutor();
  const index_t out_width = output_tensor->dim(2);

  MACE_OUT_OF_RANGE_DEFINITION;

  if (kernel->get() == nullptr) {
    std::string obfuscated_kernel_name;
    std::set<std::string> built_options;
    MACE_OUT_OF_RANGE_CONFIG;
    MACE_NON_UNIFORM_WG_CONFIG;
    if (wino_blk_size == 4) {
      obfuscated_kernel_name =
          MACE_OBFUSCATE_SYMBOL("winograd_transform_4x4");
      built_options.emplace("-Dwinograd_transform_4x4="
                                + obfuscated_kernel_name);
    } else if (wino_blk_size == 2) {
      obfuscated_kernel_name =
          MACE_OBFUSCATE_SYMBOL("winograd_transform_2x2");
      built_options.emplace("-Dwinograd_transform_2x2="
                                + obfuscated_kernel_name);
    } else {
      MACE_CHECK(false, "mace only supports 4x4 and 2x2 gpu winograd.");
      return MaceStatus::MACE_SUCCESS;
    }
    built_options.emplace("-DDATA_TYPE=" + DtToCLDt(DT_FLOAT));
    built_options.emplace("-DCMD_DATA_TYPE=" + DtToCLCMDDt(DT_FLOAT));
    MACE_RETURN_IF_ERROR(executor->BuildKernel("winograd_transform",
                                               obfuscated_kernel_name,
                                               built_options,
                                               kernel));

    *kwg_size =
        static_cast<uint32_t>(executor->GetKernelMaxWorkGroupSize(*kernel));
  }

  const uint32_t gws[2] = {
      static_cast<uint32_t>(out_width),
      static_cast<uint32_t>(RoundUpDiv4(input_tensor->dim(3)))
  };
  MACE_OUT_OF_RANGE_INIT(*kernel);
  if (input_changed) {
    uint32_t idx = 0;
    MACE_OUT_OF_RANGE_SET_ARGS(*kernel);
    MACE_SET_2D_GWS_ARGS(*kernel, gws);
    kernel->setArg(idx++, *(input_tensor->memory<cl::Image>()));
    kernel->setArg(idx++, *(output_tensor->mutable_memory<cl::Image>()));
    kernel->setArg(idx++, static_cast<uint32_t>(input_tensor->dim(1)));
    kernel->setArg(idx++, static_cast<uint32_t>(input_tensor->dim(2)));
    kernel->setArg(idx++, static_cast<uint32_t>(input_tensor->dim(3)));
    kernel->setArg(idx++, static_cast<uint32_t>(round_h * round_w));
    kernel->setArg(idx++, static_cast<uint32_t>(round_w));
    kernel->setArg(idx++, static_cast<uint32_t>(paddings[0] / 2));
    kernel->setArg(idx++, static_cast<uint32_t>(paddings[1] / 2));
  }

  const std::vector<uint32_t> lws = {*kwg_size / 8, 8, 0};
  std::string tuning_key = Concat("winograd_transform_kernel",
                                  output_tensor->dim(0),
                                  output_tensor->dim(1),
                                  output_tensor->dim(2));
  MACE_RETURN_IF_ERROR(TuningOrRun2DKernel(executor, *kernel, tuning_key,
                                           gws, lws, future));

  MACE_OUT_OF_RANGE_VALIDATION;
  return MaceStatus::MACE_SUCCESS;
}

MaceStatus WinogradOutputTransform(OpContext *context,
                                   cl::Kernel *kernel,
                                   const Tensor *input_tensor,
                                   const Tensor *bias,
                                   const index_t round_h,
                                   const index_t round_w,
                                   const int wino_blk_size,
                                   const ActivationType activation,
                                   const float relux_max_limit,
                                   const float activation_coefficient,
                                   const bool input_changed,
                                   Tensor *output_tensor,
                                   uint32_t *kwg_size,
                                   StatsFuture *future) {
  OpenclExecutor *executor = OpenclRuntime::Get(context)->GetOpenclExecutor();
  auto &output_shape = output_tensor->shape();

  MACE_OUT_OF_RANGE_DEFINITION;
  if (kernel->get() == nullptr) {
    std::string obfuscated_kernel_name;
    std::set<std::string> built_options;
    MACE_OUT_OF_RANGE_CONFIG;
    MACE_NON_UNIFORM_WG_CONFIG;
    if (wino_blk_size == 4) {
      obfuscated_kernel_name =
          MACE_OBFUSCATE_SYMBOL("winograd_inverse_transform_4x4");
      built_options.emplace("-Dwinograd_inverse_transform_4x4="
                                + obfuscated_kernel_name);
    } else if (wino_blk_size == 2) {
      obfuscated_kernel_name =
          MACE_OBFUSCATE_SYMBOL("winograd_inverse_transform_2x2");
      built_options.emplace("-Dwinograd_inverse_transform_2x2="
                                + obfuscated_kernel_name);
    } else {
      MACE_CHECK(false, "mace only supports 4x4 and 2x2 gpu winograd.");
      return MaceStatus::MACE_SUCCESS;
    }

    built_options.emplace("-DDATA_TYPE=" + DtToCLDt(DT_FLOAT));
    built_options.emplace("-DCMD_DATA_TYPE=" + DtToCLCMDDt(DT_FLOAT));
    built_options.emplace(bias != nullptr ? "-DBIAS" : "");
    common::utils::FillBuiltOptions(&built_options, activation);

    MACE_RETURN_IF_ERROR(executor->BuildKernel("winograd_transform",
                                               obfuscated_kernel_name,
                                               built_options,
                                               kernel));

    *kwg_size =
        static_cast<uint32_t>(executor->GetKernelMaxWorkGroupSize(*kernel));
  }

  const uint32_t gws[2] = {
      static_cast<uint32_t>(input_tensor->dim(2)),
      static_cast<uint32_t>(RoundUpDiv4(input_tensor->dim(1)))};
  MACE_OUT_OF_RANGE_INIT(*kernel);
  if (input_changed) {
    uint32_t idx = 0;
    MACE_OUT_OF_RANGE_SET_ARGS(*kernel);
    MACE_SET_2D_GWS_ARGS(*kernel, gws);
    kernel->setArg(
        idx++,
        *(static_cast<const cl::Image2D *>(input_tensor->memory<cl::Image>())));
    if (bias != nullptr) {
      kernel->setArg(idx++, *(static_cast<const cl::Image2D *>(
          bias->memory<cl::Image>())));
    }
    kernel->setArg(idx++, *(static_cast<cl::Image2D *>(
        output_tensor->mutable_memory<cl::Image>())));
    kernel->setArg(idx++, static_cast<uint32_t>(output_shape[1]));
    kernel->setArg(idx++, static_cast<uint32_t>(output_shape[2]));
    kernel->setArg(idx++, static_cast<uint32_t>(round_h * round_w));
    kernel->setArg(idx++, static_cast<uint32_t>(round_w));
    kernel->setArg(idx++, relux_max_limit);
    kernel->setArg(idx++, activation_coefficient);
  }
  const std::vector<uint32_t> lws = {*kwg_size / 8, 8, 0};
  std::string tuning_key =
      Concat("winograd_inverse_transform_kernel", output_tensor->dim(0),
             output_tensor->dim(1), output_tensor->dim(2),
             output_tensor->dim(3), input_tensor->dim(2));
  MACE_RETURN_IF_ERROR(TuningOrRun2DKernel(executor, *kernel, tuning_key,
                                           gws, lws, future));

  MACE_OUT_OF_RANGE_VALIDATION;
  return MaceStatus::MACE_SUCCESS;
}
}  // namespace


extern MaceStatus WinogradConv2dK3x3S1(OpContext *context,
                                       cl::Kernel *kernels[3],
                                       const Tensor *input,
                                       const Tensor *filter,
                                       const Tensor *bias,
                                       const int *paddings,
                                       const ActivationType activation,
                                       const float relux_max_limit,
                                       const float activation_coefficient,
                                       const int wino_blk_size,
                                       std::vector<index_t> *prev_input_shape,
                                       Tensor *output,
                                       uint32_t *kwg_size[3]) {
  auto *executor = OpenclRuntime::Get(context)->GetOpenclExecutor();
  StatsFuture t_input_future, mm_future, t_output_future;
  bool input_changed =
      IsResetArgsNeeded(context, *prev_input_shape, input->shape());
  *prev_input_shape = input->shape();

  auto &output_shape = output->shape();
  const index_t round_h =
      (output_shape[1] + wino_blk_size - 1) / wino_blk_size;
  const index_t round_w =
      (output_shape[2] + wino_blk_size - 1) / wino_blk_size;
  const index_t out_width = input->dim(0) * round_h * round_w;

  const index_t blk_sqr = (wino_blk_size + 2) * (wino_blk_size + 2);

  index_t in_channel = input->dim(3);
  index_t out_channel = output->dim(3);

  // 0. transform input
  // input(NHWC) -> t_input(blk_sqr, in_channel, out_width)
  std::vector<index_t> t_input_shape = {blk_sqr, in_channel, out_width};
  auto *runtime = context->runtime();
  auto transformed_input =
      make_unique<Tensor>(runtime, input->dtype(), input->memory_type(),
                          t_input_shape, false, "",
                          BufferContentType::IN_OUT_HEIGHT);
  runtime->AllocateBufferForTensor(transformed_input.get(), RENT_SCRATCH);

  MACE_RETURN_IF_ERROR(WinogradInputTransform(
      context, kernels[0], input, paddings,
      round_h, round_w, wino_blk_size,
      input_changed, transformed_input.get(),
      kwg_size[0], &t_input_future));

  // 1. mat mul
  // t_filter(blk_sqr, out_chan, in_chan)*t_input(blk_sqr, in_chan, out_width)
  //     -> t_output (blk_sqr, out_chan, out_width)
  std::vector<index_t> mm_output_shape =
      {blk_sqr, out_channel, out_width};
  std::unique_ptr<Tensor> mm_output = make_unique<Tensor>(
      runtime, input->dtype(), input->memory_type(), mm_output_shape,
      false, "", BufferContentType::IN_OUT_HEIGHT);
  runtime->AllocateBufferForTensor(mm_output.get(), RENT_SCRATCH);

  const index_t height_blocks = RoundUpDiv4(mm_output_shape[1]);
  const index_t width_blocks = RoundUpDiv4(mm_output_shape[2]);
  const uint32_t gws[2] = {
      static_cast<uint32_t>(width_blocks),
      static_cast<uint32_t>(height_blocks * blk_sqr),
  };

  MACE_OUT_OF_RANGE_DEFINITION;

  if (kernels[1]->get() == nullptr) {
    std::set<std::string> built_options;
    MACE_OUT_OF_RANGE_CONFIG;
    MACE_NON_UNIFORM_WG_CONFIG;
    std::string kernel_name = MACE_OBFUSCATE_SYMBOL("matmul");
    built_options.emplace("-Dmatmul=" + kernel_name);
    built_options.emplace("-DDATA_TYPE=" + DtToCLDt(DT_FLOAT));
    built_options.emplace("-DCMD_DATA_TYPE=" + DtToCLCMDDt(DT_FLOAT));
    MACE_RETURN_IF_ERROR(executor->BuildKernel("matmul", kernel_name,
                                               built_options, kernels[1]));

    *kwg_size[1] =
        static_cast<uint32_t>(executor->GetKernelMaxWorkGroupSize(*kernels[1]));
  }
  MACE_OUT_OF_RANGE_INIT(*kernels[1]);
  uint32_t idx = 0;
  MACE_OUT_OF_RANGE_SET_ARGS(*kernels[1]);
  MACE_SET_2D_GWS_ARGS(*kernels[1], gws);
  kernels[1]->setArg(idx++, *(filter->memory<cl::Image>()));
  kernels[1]->setArg(idx++, *(transformed_input->memory<cl::Image>()));
  kernels[1]->setArg(idx++, *(mm_output->mutable_memory<cl::Image>()));
  kernels[1]->setArg(idx++, static_cast<int>(mm_output_shape[1]));
  kernels[1]->setArg(idx++, static_cast<int>(mm_output_shape[2]));
  kernels[1]->setArg(idx++, static_cast<int>(in_channel));
  kernels[1]->setArg(idx++, static_cast<int>(height_blocks));
  kernels[1]->setArg(idx++, static_cast<int>(RoundUpDiv4(in_channel)));

  const std::vector<uint32_t> lws = {*kwg_size[1] / 64, 64, 0};
  std::string tuning_key = Concat("matmul_opencl_kernel", mm_output_shape[0],
                                  mm_output_shape[1], mm_output_shape[2]);
  MACE_RETURN_IF_ERROR(TuningOrRun2DKernel(executor, *kernels[1], tuning_key,
                                           gws, lws, &mm_future));

  MACE_OUT_OF_RANGE_VALIDATION;

  // 2. transform output
  // t_output (blk_sqr, out_chan, out_width) -> output(NHWC)
  MACE_RETURN_IF_ERROR(WinogradOutputTransform(
      context, kernels[2], mm_output.get(), bias,
      round_h, round_w, wino_blk_size, activation, relux_max_limit,
      activation_coefficient, input_changed, output, kwg_size[2],
      &t_output_future))

  MergeMultipleFutureWaitFn({t_input_future, mm_future, t_output_future},
                            context->future());
  return MaceStatus::MACE_SUCCESS;
}

}  // namespace image
}  // namespace opencl
}  // namespace ops
}  // namespace mace
