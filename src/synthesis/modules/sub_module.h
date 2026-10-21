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
#include "sub_oscillator.h"

namespace vital {
  class SubModule : public SynthModule {
    public:
      enum {
        kReset,
        kMidi,
        kNumInputs
      };

      enum {
        kRaw,
        kLevelled,
        kNumOutputs
      };

      SubModule();
      virtual ~SubModule() { }

      void process(int num_samples) override;
      void init() override;
      virtual Processor* clone() const override { return new SubModule(*this); }

    protected:
      std::shared_ptr<bool> was_on_;
      SubOscillator* oscillator_;
      Value* on_;

      JUCE_LEAK_DETECTOR(SubModule)
  };
} // namespace vital
