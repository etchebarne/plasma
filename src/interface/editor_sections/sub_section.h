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

#include "JuceHeader.h"
#include "synth_section.h"

class SynthButton;
class TextSelector;

class SubSection : public SynthSection {
  public:
    SubSection(const String& name);
    ~SubSection();

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { if (isActive()) paintTabShadow(g); }
    void resized() override;
    void sliderValueChanged(Slider* changed_slider) override;
    void setAllValues(vital::control_map& controls) override;

  private:
    Rectangle<int> waveformBounds() const;
    void paintWaveform(Graphics& g);

    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<SynthSlider> transpose_;
    std::unique_ptr<SynthSlider> tune_;
    std::unique_ptr<TextSelector> waveform_;
    std::unique_ptr<TextSelector> destination_;
    std::unique_ptr<SynthSlider> level_;
    std::unique_ptr<SynthSlider> pan_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SubSection)
};
