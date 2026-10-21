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

class HyperViewer;
class SynthButton;

class HyperSection : public SynthSection {
  public:
    HyperSection(const String& name);
    ~HyperSection();

    void paintBackground(Graphics& g) override;
    void paintBackgroundShadow(Graphics& g) override { if (isActive()) paintTabShadow(g); }
    void resized() override;
    void setActive(bool active) override;

  private:
    std::unique_ptr<SynthButton> on_;
    std::unique_ptr<SynthSlider> voices_;
    std::unique_ptr<SynthSlider> rate_;
    std::unique_ptr<SynthSlider> detune_;
    std::unique_ptr<SynthSlider> dry_wet_;
    std::unique_ptr<SynthSlider> dimension_size_;
    std::unique_ptr<SynthSlider> dimension_mix_;
    std::unique_ptr<HyperViewer> viewer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HyperSection)
};
