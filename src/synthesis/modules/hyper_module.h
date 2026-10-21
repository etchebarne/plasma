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

#pragma once

#include "synth_module.h"

#include <vector>

namespace vital {

  // Serum-style Hyper/Dimension effect.
  // Hyper: up to kMaxVoices modulated short delays reading one stereo ring buffer, spread across the
  // stereo field like unison voices. Detune is specified as pitch deviation, so the modulation depth
  // shrinks as the rate rises and the detune amount sounds the same at any rate.
  // Dimension: two slowly modulated delays of the mid signal added back as side content, which widens
  // the image while cancelling in mono.
  class HyperModule : public SynthModule {
    public:
      static constexpr int kMinVoices = 2;
      static constexpr int kMaxVoices = 7;
      static constexpr mono_float kMaxPitchDeviation = 0.012f;
      static constexpr mono_float kMinDelay = 0.002f;
      static constexpr mono_float kVoiceDelaySpacing = 0.0013f;
      static constexpr mono_float kMaxModDelay = 0.015f;
      static constexpr mono_float kBufferSeconds = 0.05f;

      HyperModule();
      virtual ~HyperModule() { }

      void init() override;
      void enable(bool enable) override;
      void hardReset() override;

      void processWithInput(const poly_float* audio_in, int num_samples) override;
      Processor* clone() const override { VITAL_ASSERT(false); return nullptr; }

    private:
      force_inline float read(const std::vector<float>& buffer, float delay_samples) const;
      void clearBuffers();

      Value* voices_;
      Output* rate_;
      Output* detune_;
      Output* wet_output_;
      Output* dimension_size_;
      Output* dimension_mix_;

      std::vector<float> buffer_left_;
      std::vector<float> buffer_right_;
      std::vector<float> buffer_mid_;
      int buffer_mask_;
      int write_index_;

      float voice_phases_[kMaxVoices];
      float dimension_phases_[2];
      float last_depth_;
      float last_dimension_size_;
      poly_float wet_;
      poly_float dry_;

      JUCE_LEAK_DETECTOR(HyperModule)
  };
} // namespace vital
