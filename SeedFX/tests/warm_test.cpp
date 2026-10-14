#include "../../Software/GuitarPedal/Effect-Modules/Nam/model_data_nam_a2.h"
#include "../firmware/audio/warm_start.hpp"
#include <cstdio>
#include <random>
int main() {
  using namespace nam_a2_daisy;
  std::mt19937 rng(1701);
  float worst = 0;
  for (const auto &model : nam_a2_models::kNamA2Models) {
    A2SharedWeights weights{};
    A2State simulated{}, analytic{};
    A2HotState old_hot{}, new_hot{};
    if (!load_weights(weights, model.weights, kA2WeightCount))
      return 1;
    prewarm(simulated, old_hot, weights);
    if (!seedfx::steady_start(analytic, new_hot, weights))
      return 1;
    for (int block = 0; block < 64; ++block) {
      float input[48], a[48], b[48];
      for (int n = 0; n < 48; ++n)
        input[n] = 0.1f * std::sin(0.04f * (block * 48 + n)) +
                   0.02f * (int(rng() % 1000) - 500) / 500;
      process_block_48(simulated, old_hot, weights, input, a);
      process_block_48(analytic, new_hot, weights, input, b);
      for (int n = 0; n < 48; ++n) {
        float error = std::fabs(a[n] - b[n]);
        worst = std::max(worst, error);
        if (!std::isfinite(a[n]) || !std::isfinite(b[n]) ||
            error > 0.0001f + 0.0001f * std::fabs(a[n])) {
          std::fprintf(stderr,
                       "Warm-start mismatch %s block=%d sample=%d: %g vs %g\n",
                       model.name, block, n, double(a[n]), double(b[n]));
          return 1;
        }
      }
    }
  }
  std::printf("Analytic warm start matches 133-block reference: 5 models, "
              "15360 samples; max absolute error %.9g.\n",
              double(worst));
}
