/* Copyright 2026 Plasma contributors
 *
 * Plasma is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Plasma is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Plasma.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "formula_wavetable.h"

#include "formula.h"
#include "synth_constants.h"
#include "wave_frame.h"
#include "wave_source.h"

namespace formula_wavetable {
  const Example kExamples[] = {
    { "Sine", "sin(pi * x)" },
    { "Saw to Square", "lerp(-x, square(t), y)" },
    { "Pulse Width Sweep", "pulse(t, 0.05 + 0.9 * y)" },
    { "Sync Sweep", "sin(tau * t * (1 + 7 * y)) * (1 - t)" },
    { "Harmonic Stack", "sin(pi * x) + 0.5 * y * sin(3 * pi * x) + 0.33 * y * sin(5 * pi * x)" },
    { "Soft to Hard Clip", "tanh(sin(pi * x) * (1 + 20 * y))" },
    { "Wavefold", "sin(sin(pi * x) * (1 + 6 * y) * pi / 2)" },
    { "Bitcrush", "round(sin(pi * x) * (16 - 15 * y)) / (16 - 15 * y)" },
    { "Formant Vowel", "sin(pi * x) * sin(tau * t * (3 + 9 * y)) * (1 - abs(x))" },
    { "Noise Morph", "lerp(sin(pi * x), rand(), y)" },
  };
  const int kNumExamples = sizeof(kExamples) / sizeof(Example);

  json createWavetableJson(const std::string& formula_text, std::string& error) {
    std::unique_ptr<Formula> formula = Formula::parse(formula_text, error);
    if (formula == nullptr)
      return json();

    static constexpr int kWaveformSize = vital::WaveFrame::kWaveformSize;
    int num_keyframes = formula->usesTablePosition() ? kTableKeyframes : 1;
    int last_position = vital::kNumOscillatorWaveFrames - 1;

    WaveSource source;
    Formula::Variables variables;
    for (int k = 0; k < num_keyframes; ++k) {
      int position = num_keyframes == 1 ? 0 : (k * last_position) / (num_keyframes - 1);
      WaveSourceKeyframe* keyframe = dynamic_cast<WaveSourceKeyframe*>(source.insertNewKeyframe(position));
      float* samples = keyframe->wave_frame()->time_domain;

      variables.y = num_keyframes == 1 ? 0.0 : k / (num_keyframes - 1.0);
      variables.frame = k;
      for (int i = 0; i < kWaveformSize; ++i) {
        variables.t = i / (double)kWaveformSize;
        variables.x = 2.0 * variables.t - 1.0;
        variables.sample = i;
        samples[i] = (float)formula->evaluate(variables);
      }
    }

    json group = { { "components", json::array({ source.stateToJson() }) } };
    return {
      { "groups", json::array({ group }) },
      { "name", "Formula" },
      { "author", "" },
      { "version", ProjectInfo::versionString },
      { "remove_all_dc", true },
      { "full_normalize", true },
      { "formula", formula_text },
    };
  }
}
