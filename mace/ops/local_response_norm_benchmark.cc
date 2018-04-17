//
// Copyright (c) 2017 XiaoMi All rights reserved.
//

#include "mace/core/operator.h"
#include "mace/core/testing/test_benchmark.h"
#include "mace/ops/ops_test_util.h"

namespace mace {
namespace ops {
namespace test {

template <DeviceType D, typename T>
static void LocalResponseNorm(
    int iters, int batch, int channels, int height, int width) {
  mace::testing::StopTiming();

  OpsTestNet net;

  // Add input data
  net.AddRandomInput<D, T>("Input", {batch, height, width, channels});

  OpDefBuilder("LocalResponseNorm", "LocalResponseNormBM")
      .Input("Input")
      .Output("Output")
      .Finalize(net.NewOperatorDef());

  // tuning
  setenv("MACE_TUNING", "1", 1);
  net.RunOp(D);
  unsetenv("MACE_TUNING");

  // Warm-up
  for (int i = 0; i < 5; ++i) {
    net.RunOp(D);
  }
  net.Sync();

  mace::testing::StartTiming();
  while (iters--) {
    net.RunOp(D);
  }
  net.Sync();
}

#define BM_LOCAL_RESPONSE_NORM_MACRO(N, C, H, W, TYPE, DEVICE)                 \
  static void BM_LOCAL_RESPONSE_NORM_##N##_##C##_##H##_##W##_##TYPE##_##DEVICE(\
      int iters) {                                                             \
    const int64_t tot = static_cast<int64_t>(iters) * N * C * H * W;           \
    mace::testing::MaccProcessed(tot);                                         \
    mace::testing::BytesProcessed(tot *(sizeof(TYPE)));                        \
    LocalResponseNorm<DEVICE, TYPE>(iters, N, C, H, W);                        \
  }                                                                            \
  BENCHMARK(BM_LOCAL_RESPONSE_NORM_##N##_##C##_##H##_##W##_##TYPE##_##DEVICE)

#define BM_LOCAL_RESPONSE_NORM(N, C, H, W)                 \
  BM_LOCAL_RESPONSE_NORM_MACRO(N, C, H, W, float, CPU);    \
  BM_LOCAL_RESPONSE_NORM_MACRO(N, C, H, W, float, NEON);

BM_LOCAL_RESPONSE_NORM(1, 1, 512, 512);
BM_LOCAL_RESPONSE_NORM(1, 3, 128, 128);
BM_LOCAL_RESPONSE_NORM(1, 3, 512, 512);
BM_LOCAL_RESPONSE_NORM(1, 32, 112, 112);
BM_LOCAL_RESPONSE_NORM(1, 64, 256, 256);
BM_LOCAL_RESPONSE_NORM(1, 64, 512, 512);
BM_LOCAL_RESPONSE_NORM(1, 128, 56, 56);
BM_LOCAL_RESPONSE_NORM(1, 128, 256, 256);
BM_LOCAL_RESPONSE_NORM(1, 256, 14, 14);
BM_LOCAL_RESPONSE_NORM(1, 512, 14, 14);
BM_LOCAL_RESPONSE_NORM(1, 1024, 7, 7);
BM_LOCAL_RESPONSE_NORM(32, 1, 256, 256);
BM_LOCAL_RESPONSE_NORM(32, 3, 256, 256);

}  // namespace test
}  // namespace ops
}  // namespace mace
