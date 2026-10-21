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

#include "sub_module.h"

namespace vital {

  SubModule::SubModule() : SynthModule(kNumInputs, kNumOutputs), on_(nullptr) {
    oscillator_ = new SubOscillator();
    was_on_ = std::make_shared<bool>(true);
  }

  void SubModule::init() {
    on_ = createBaseControl("sub_on");
    Value* waveform = createBaseControl("sub_waveform");
    Output* transpose = createPolyModControl("sub_transpose");
    Output* tune = createPolyModControl("sub_tune");
    Output* level = createPolyModControl("sub_level", true, true);
    Output* pan = createPolyModControl("sub_pan");

    oscillator_->useInput(input(kReset), SubOscillator::kReset);
    oscillator_->useInput(input(kMidi), SubOscillator::kMidi);
    oscillator_->plug(waveform, SubOscillator::kWaveform);
    oscillator_->plug(transpose, SubOscillator::kTranspose);
    oscillator_->plug(tune, SubOscillator::kTune);
    oscillator_->plug(level, SubOscillator::kLevel);
    oscillator_->plug(pan, SubOscillator::kPan);
    oscillator_->useOutput(output(kRaw), SubOscillator::kRaw);
    oscillator_->useOutput(output(kLevelled), SubOscillator::kLevelled);

    addProcessor(oscillator_);
    SynthModule::init();
  }

  void SubModule::process(int num_samples) {
    bool on = on_->value();

    if (on)
      SynthModule::process(num_samples);
    else if (*was_on_) {
      output(kRaw)->clearBuffer();
      output(kLevelled)->clearBuffer();
    }

    *was_on_ = on;
  }
} // namespace vital
