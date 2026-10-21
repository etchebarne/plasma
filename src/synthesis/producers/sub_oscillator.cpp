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

#include "sub_oscillator.h"

#include "futils.h"
#include "synth_constants.h"

namespace vital {

  namespace {
    // Polynomial band-limited step correction for a discontinuity at phase 0.
    force_inline poly_float polyBlep(poly_float phase, poly_float phase_inc) {
      poly_mask start = poly_float::lessThan(phase, phase_inc);
      poly_mask end = poly_float::greaterThan(phase, poly_float(1.0f) - phase_inc);
      poly_float x_start = phase / phase_inc;
      poly_float x_end = (phase - 1.0f) / phase_inc;
      poly_float start_value = x_start * 2.0f - x_start * x_start - 1.0f;
      poly_float end_value = x_end * x_end + x_end * 2.0f + 1.0f;
      return (start_value & start) + (end_value & end);
    }

    force_inline poly_float wrap(poly_float phase) {
      return phase - utils::floor(phase);
    }

    force_inline poly_float square(poly_float phase, poly_float phase_inc, mono_float width) {
      poly_mask high = poly_float::lessThan(phase, width);
      poly_float value = utils::maskLoad(poly_float(-1.0f), poly_float(1.0f), high);
      // Offset so the pulse has no DC at any width.
      value += 1.0f - 2.0f * width;
      return value + polyBlep(phase, phase_inc) - polyBlep(wrap(phase + (1.0f - width)), phase_inc);
    }
  }

  SubOscillator::SubOscillator() : Processor(kNumInputs, kNumOutputs), phase_(0.0f),
                                   phase_inc_(0.0f), pan_amplitude_(0.0f) { }

  void SubOscillator::process(int num_samples) {
    poly_float current_pan_amplitude = pan_amplitude_;
    poly_float input_pan = utils::clamp(input(kPan)->at(0), -1.0f, 1.0f);
    pan_amplitude_ = futils::panAmplitude(input_pan);

    poly_float transpose = input(kTranspose)->at(0) + input(kTune)->at(0);
    poly_float midi = utils::clamp(input(kMidi)->at(0) + transpose, kMinNote, kMaxNote);
    poly_float frequency = utils::midiNoteToFrequency(midi);
    poly_float max_inc = 0.5f;
    poly_float current_phase_inc = phase_inc_;
    phase_inc_ = utils::min(frequency * (1.0f / getSampleRate()), max_inc);

    poly_mask reset_mask = getResetMask(kReset);
    current_phase_inc = utils::maskLoad(current_phase_inc, phase_inc_, reset_mask);
    current_pan_amplitude = utils::maskLoad(current_pan_amplitude, pan_amplitude_, reset_mask);
    // Start new notes at phase 0 on the exact sample they trigger.
    poly_float reset_offset = utils::toFloat(input(kReset)->source->trigger_offset);
    phase_ = utils::maskLoad(phase_, wrap(-reset_offset * phase_inc_), reset_mask);

    mono_float sample_inc = 1.0f / num_samples;
    poly_float delta_phase_inc = (phase_inc_ - current_phase_inc) * sample_inc;
    poly_float delta_pan_amplitude = (pan_amplitude_ - current_pan_amplitude) * sample_inc;

    int waveform = utils::iclamp(input(kWaveform)->at(0)[0], 0, kNumWaveforms - 1);
    poly_float rounded_scale = 1.0f / futils::tanh(kRoundedDrive);

    poly_float* raw_output = output(kRaw)->buffer;
    poly_float phase = phase_;
    for (int i = 0; i < num_samples; ++i) {
      current_phase_inc += delta_phase_inc;
      poly_float value;
      switch (waveform) {
        case kTriangle:
          value = poly_float(1.0f) - poly_float::abs(phase * 4.0f - 2.0f);
          break;
        case kSaw:
          value = phase * 2.0f - 1.0f - polyBlep(phase, current_phase_inc);
          break;
        case kSquare:
          value = square(phase, current_phase_inc, 0.5f);
          break;
        case kPulse:
          value = square(phase, current_phase_inc, kPulseWidth);
          break;
        case kRounded:
          value = futils::tanh(futils::sin1(phase) * kRoundedDrive) * rounded_scale;
          break;
        default:
          value = futils::sin1(phase);
          break;
      }
      raw_output[i] = value;
      phase = wrap(phase + current_phase_inc);
    }
    phase_ = phase;

    if (reset_mask.anyMask())
      clearOutputBufferForReset(reset_mask, kReset, kRaw);

    const poly_float* level_input = input(kLevel)->source->buffer;
    poly_float* levelled_output = output(kLevelled)->buffer;
    poly_float zero = 0.0f;
    poly_float max = kMaxAmplitude;
    for (int i = 0; i < num_samples; ++i) {
      current_pan_amplitude += delta_pan_amplitude;
      poly_float level = utils::clamp(level_input[i], zero, max);
      levelled_output[i] = current_pan_amplitude * level * level * raw_output[i];
    }
  }
} // namespace vital
