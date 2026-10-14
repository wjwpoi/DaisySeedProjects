#pragma once
#include "../../../Software/GuitarPedal/Effect-Modules/Nam/nam_a2_runtime.h"
#include <algorithm>
#include <cmath>

namespace seedfx {
// The network is a feed-forward causal FIR/residual cascade. For zero input,
// each layer has a constant steady-state input; no recurrent fixed-point solve
// is necessary. Fill each delay history with that constant and initialize the
// head with the sum of the constant activations. This replaces 133 simulated
// blocks with one history fill plus one evaluation of each layer's DC response.
// Numerical equivalence is tested against the original complete prewarm.
inline bool steady_start(nam_a2_daisy::A2State &state,
                         nam_a2_daisy::A2HotState &hot,
                         const nam_a2_daisy::A2SharedWeights &weights) {
  using namespace nam_a2_daisy;
  if (!weights.loaded)
    return false;
  float input[3]{}, head[3]{};
  for (int li = 0; li < kNumLayers; ++li) {
    float *history = state.history + kLayerHistoryOffset[li];
    for (int column = 0; column < kLayerCols[li]; ++column)
      for (int c = 0; c < 3; ++c)
        history[column * 3 + c] = input[c];
    state.layerWritePos[li] = kLayerCols[li] - 1;
    const auto &layer = weights.layer[li];
    const auto *conv = weights.convW + kLayerConvOffset[li];
    const auto *residual = weights.l1x1W + kLayerL1Offset[li];
    float activation[3]{};
    for (int out = 0; out < 3; ++out) {
      float sum = layer.convB[out];
      for (int tap = 0; tap < kKernelSizes[li]; ++tap)
        sum += conv[tap * 9 + out] * input[0] +
               conv[tap * 9 + 3 + out] * input[1] +
               conv[tap * 9 + 6 + out] * input[2];
      activation[out] = sum >= 0 ? sum : kLeakySlope * sum;
      head[out] += activation[out];
    }
    float next[3]{};
    for (int out = 0; out < 3; ++out) {
      next[out] =
          input[out] + layer.l1x1B[out] + activation[0] * residual[out] +
          activation[1] * residual[3 + out] + activation[2] * residual[6 + out];
      if (!std::isfinite(next[out]) || !std::isfinite(head[out]))
        return false;
    }
    std::copy(next, next + 3, input);
  }
  for (int tap = 0; tap < kHeadKernel; ++tap)
    for (int c = 0; c < 3; ++c)
      hot.headHistory[tap * 3 + c] = head[c];
  hot.headWritePos = kHeadKernel - 1;
  std::fill(std::begin(hot.bufA), std::end(hot.bufA), 0);
  std::fill(std::begin(hot.bufB), std::end(hot.bufB), 0);
  std::fill(std::begin(hot.headSum), std::end(hot.headSum), 0);
  return true;
}
} // namespace seedfx
