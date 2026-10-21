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

#include "hyper_module.h"

#include "futils.h"
#include "synth_constants.h"

#include <cmath>

namespace vital {

  namespace {
    // Each voice runs its LFO at a slightly different rate so the voices never line up.
    constexpr float kVoiceRateSpread[HyperModule::kMaxVoices] = {
      1.0f, 1.13f, 0.89f, 1.27f, 0.79f, 1.41f, 0.71f
    };
    constexpr float kVoiceStartPhases[HyperModule::kMaxVoices] = {
      0.0f, 0.43f, 0.71f, 0.17f, 0.89f, 0.31f, 0.57f
    };
    constexpr float kVoiceDepthScale[HyperModule::kMaxVoices] = {
      1.0f, 0.82f, 0.94f, 0.67f, 0.88f, 0.74f, 0.97f
    };
    constexpr float kRightPhaseOffset = 0.25f;

    constexpr float kDimensionRates[2] = { 0.27f, 0.19f };
    constexpr float kDimensionMinDelays[2] = { 0.004f, 0.006f };
    constexpr float kDimensionDelayRanges[2] = { 0.012f, 0.018f };
    constexpr float kDimensionModDelay = 0.0012f;
    constexpr float kDimensionGain = 0.35f;

    force_inline float sinCycle(float phase) {
      return std::sin(phase * 2.0f * kPi);
    }

    force_inline float wrap(float phase) {
      return phase - std::floor(phase);
    }
  }

  HyperModule::HyperModule() : SynthModule(0, 1), voices_(nullptr), rate_(nullptr), detune_(nullptr),
                               wet_output_(nullptr), dimension_size_(nullptr), dimension_mix_(nullptr),
                               write_index_(0), last_depth_(0.0f), last_dimension_size_(0.0f) {
    int max_samples = kBufferSeconds * kMaxSampleRate * kMaxOversample + 4;
    int size = 1;
    while (size < max_samples)
      size *= 2;

    buffer_left_.resize(size, 0.0f);
    buffer_right_.resize(size, 0.0f);
    buffer_mid_.resize(size, 0.0f);
    buffer_mask_ = size - 1;
    wet_ = 0.0f;
    dry_ = 0.0f;

    for (int i = 0; i < kMaxVoices; ++i)
      voice_phases_[i] = kVoiceStartPhases[i];
    dimension_phases_[0] = 0.0f;
    dimension_phases_[1] = 0.5f;
  }

  void HyperModule::init() {
    voices_ = createBaseControl("hyper_voices");
    rate_ = createMonoModControl("hyper_rate");
    detune_ = createMonoModControl("hyper_detune");
    wet_output_ = createMonoModControl("hyper_dry_wet");
    dimension_size_ = createMonoModControl("hyper_dimension_size");
    dimension_mix_ = createMonoModControl("hyper_dimension_mix");

    SynthModule::init();
  }

  void HyperModule::enable(bool enable) {
    SynthModule::enable(enable);
    process(1);
    if (enable) {
      wet_ = 0.0f;
      dry_ = 0.0f;
      clearBuffers();
    }
  }

  void HyperModule::hardReset() {
    SynthModule::hardReset();
    clearBuffers();
  }

  void HyperModule::clearBuffers() {
    std::fill(buffer_left_.begin(), buffer_left_.end(), 0.0f);
    std::fill(buffer_right_.begin(), buffer_right_.end(), 0.0f);
    std::fill(buffer_mid_.begin(), buffer_mid_.end(), 0.0f);
  }

  // 4-point Hermite read, delay_samples behind the most recently written sample.
  force_inline float HyperModule::read(const std::vector<float>& buffer, float delay_samples) const {
    delay_samples = std::max(1.0f, std::min(delay_samples, (float)(buffer_mask_ - 3)));
    float position = write_index_ - delay_samples;
    int index = (int)std::floor(position);
    float t = position - index;

    float y0 = buffer[(index - 1) & buffer_mask_];
    float y1 = buffer[index & buffer_mask_];
    float y2 = buffer[(index + 1) & buffer_mask_];
    float y3 = buffer[(index + 2) & buffer_mask_];

    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + y1;
  }

  void HyperModule::processWithInput(const poly_float* audio_in, int num_samples) {
    SynthModule::process(num_samples);

    float sample_rate = getSampleRate();
    int num_voices = utils::iclamp(voices_->value(), kMinVoices, kMaxVoices);
    float rate = std::max(0.001f, rate_->buffer[0][0]);
    float detune = utils::clamp(detune_->buffer[0][0], 0.0f, 1.0f);
    float dimension_size = utils::clamp(dimension_size_->buffer[0][0], 0.0f, 1.0f);
    float dimension_mix = utils::clamp(dimension_mix_->buffer[0][0], 0.0f, 1.0f);

    // Modulation depth in seconds for a sine delay sweep giving the requested pitch deviation.
    float depth = std::min(kMaxModDelay, detune * kMaxPitchDeviation / (2.0f * kPi * rate));
    float tick = 1.0f / num_samples;
    float delta_depth = (depth - last_depth_) * tick;
    float delta_dimension_size = (dimension_size - last_dimension_size_) * tick;

    poly_float current_wet = wet_;
    poly_float current_dry = dry_;
    poly_float wet_value = utils::clamp(wet_output_->buffer[0], 0.0f, 1.0f);
    wet_ = futils::equalPowerFade(wet_value);
    dry_ = futils::equalPowerFadeInverse(wet_value);
    float wet_start = current_wet[0];
    float dry_start = current_dry[0];
    float delta_wet = (wet_[0] - wet_start) * tick;
    float delta_dry = (dry_[0] - dry_start) * tick;

    float pan_left[kMaxVoices];
    float pan_right[kMaxVoices];
    for (int v = 0; v < num_voices; ++v) {
      float pan = 2.0f * v / (num_voices - 1.0f) - 1.0f;
      float angle = (pan + 1.0f) * 0.25f * kPi;
      pan_left[v] = std::cos(angle) * kSqrt2;
      pan_right[v] = std::sin(angle) * kSqrt2;
    }
    float voice_gain = 1.0f / std::sqrt((float)num_voices);
    float phase_delta = rate / sample_rate;

    poly_float* audio_out = output()->buffer;
    float current_depth = last_depth_;
    float current_dimension_size = last_dimension_size_;
    float wet_gain = wet_start;
    float dry_gain = dry_start;

    for (int s = 0; s < num_samples; ++s) {
      current_depth += delta_depth;
      current_dimension_size += delta_dimension_size;
      wet_gain += delta_wet;
      dry_gain += delta_dry;

      float in_left = audio_in[s][0];
      float in_right = audio_in[s][1];
      buffer_left_[write_index_] = in_left;
      buffer_right_[write_index_] = in_right;

      float hyper_left = 0.0f;
      float hyper_right = 0.0f;
      for (int v = 0; v < num_voices; ++v) {
        float voice_depth = current_depth * kVoiceDepthScale[v];
        float center = kMinDelay + kMaxModDelay + v * kVoiceDelaySpacing;
        float phase = voice_phases_[v];
        float delay_left = center + voice_depth * sinCycle(phase);
        float delay_right = center + voice_depth * sinCycle(phase + kRightPhaseOffset);

        hyper_left += pan_left[v] * read(buffer_left_, delay_left * sample_rate);
        hyper_right += pan_right[v] * read(buffer_right_, delay_right * sample_rate);
        voice_phases_[v] = wrap(phase + phase_delta * kVoiceRateSpread[v]);
      }

      float left = dry_gain * in_left + wet_gain * voice_gain * hyper_left;
      float right = dry_gain * in_right + wet_gain * voice_gain * hyper_right;

      float mid = 0.5f * (left + right);
      buffer_mid_[write_index_] = mid;
      float dimension[2];
      for (int i = 0; i < 2; ++i) {
        float delay = kDimensionMinDelays[i] + kDimensionDelayRanges[i] * current_dimension_size +
                      kDimensionModDelay * sinCycle(dimension_phases_[i]);
        dimension[i] = read(buffer_mid_, delay * sample_rate);
        dimension_phases_[i] = wrap(dimension_phases_[i] + kDimensionRates[i] / sample_rate);
      }
      float side = kDimensionGain * dimension_mix * (dimension[0] - dimension[1]);

      audio_out[s] = poly_float(left + side, right - side);
      write_index_ = (write_index_ + 1) & buffer_mask_;
    }

    last_depth_ = depth;
    last_dimension_size_ = dimension_size;
    VITAL_ASSERT(utils::isFinite(audio_out, num_samples));
  }
} // namespace vital
