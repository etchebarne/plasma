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

#include "processor.h"

namespace vital {

  // Serum-style sub oscillator: one simple waveform per voice that follows the played note, with
  // PolyBLEP antialiasing on the hard edges. The phase restarts on every note so the low end is
  // consistent from note to note.
  class SubOscillator : public Processor {
    public:
      static constexpr mono_float kMaxAmplitude = 1.41421356237f;
      static constexpr mono_float kMinNote = -48.0f;
      static constexpr mono_float kMaxNote = 160.0f;
      static constexpr mono_float kPulseWidth = 0.25f;
      static constexpr mono_float kRoundedDrive = 4.0f;

      enum Waveform {
        kSine,
        kTriangle,
        kSaw,
        kSquare,
        kPulse,
        kRounded,
        kNumWaveforms
      };

      enum {
        kReset,
        kMidi,
        kWaveform,
        kTranspose,
        kTune,
        kLevel,
        kPan,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      SubOscillator();

      virtual void process(int num_samples) override;
      virtual Processor* clone() const override { return new SubOscillator(*this); }

    private:
      poly_float phase_;
      poly_float phase_inc_;
      poly_float pan_amplitude_;

      JUCE_LEAK_DETECTOR(SubOscillator)
  };
} // namespace vital
