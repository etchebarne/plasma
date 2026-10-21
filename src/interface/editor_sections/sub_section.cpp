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

#include "sub_section.h"

#include "skin.h"
#include "sub_oscillator.h"
#include "synth_button.h"
#include "synth_slider.h"
#include "synth_strings.h"
#include "text_look_and_feel.h"
#include "text_selector.h"

namespace {
  constexpr float kPitchWidthPercent = 0.27f;
  constexpr float kWaveWidthPercent = 0.27f;
  // The waveform and destination selectors sit in short label-height rows, so size text to fit.
  constexpr float kLabelTextPercent = 0.7f;

  // Matches the shapes SubOscillator renders, without the antialiasing.
  float waveformValue(int waveform, float phase) {
    switch (waveform) {
      case vital::SubOscillator::kTriangle:
        return 1.0f - std::abs(phase * 4.0f - 2.0f);
      case vital::SubOscillator::kSaw:
        return phase * 2.0f - 1.0f;
      case vital::SubOscillator::kSquare:
        return phase < 0.5f ? 1.0f : -1.0f;
      case vital::SubOscillator::kPulse:
        return phase < vital::SubOscillator::kPulseWidth ? 1.0f : -1.0f;
      case vital::SubOscillator::kRounded: {
        float drive = vital::SubOscillator::kRoundedDrive;
        return std::tanh(drive * std::sin(2.0f * vital::kPi * phase)) / std::tanh(drive);
      }
      default:
        return std::sin(2.0f * vital::kPi * phase);
    }
  }
}

SubSection::SubSection(const String& name) : SynthSection(name) {
  static const double kTransposeMouseSensitivity = 0.2;

  transpose_ = std::make_unique<SynthSlider>("sub_transpose");
  addSlider(transpose_.get());
  transpose_->setLookAndFeel(TextLookAndFeel::instance());
  transpose_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  transpose_->setSensitivity(kTransposeMouseSensitivity);
  transpose_->setTextEntrySizePercent(1.0f, 0.7f);
  transpose_->setShiftIndexAmount(vital::kNotesPerOctave);
  transpose_->overrideValue(Skin::kTextComponentOffset, 0.0f);
  transpose_->setModulationBarRight(false);

  tune_ = std::make_unique<SynthSlider>("sub_tune");
  addSlider(tune_.get());
  tune_->setLookAndFeel(TextLookAndFeel::instance());
  tune_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  tune_->setMaxDisplayCharacters(3);
  tune_->setMaxDecimalPlaces(0);
  tune_->setTextEntrySizePercent(1.0f, 0.7f);
  tune_->overrideValue(Skin::kTextComponentOffset, 0.0f);

  waveform_ = std::make_unique<TextSelector>("sub_waveform");
  addSlider(waveform_.get());
  waveform_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  waveform_->setLookAndFeel(TextLookAndFeel::instance());
  waveform_->setTextHeightPercentage(kLabelTextPercent);
  waveform_->overrideValue(Skin::kTextComponentOffset, 0.0f);
  waveform_->setLongStringLookup(strings::kSubWaveformNames);

  destination_ = std::make_unique<TextSelector>("sub_destination");
  addSlider(destination_.get());
  destination_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  destination_->setLookAndFeel(TextLookAndFeel::instance());
  destination_->setTextHeightPercentage(kLabelTextPercent);
  destination_->overrideValue(Skin::kTextComponentOffset, 0.0f);
  destination_->setLongStringLookup(strings::kDestinationMenuNames);

  level_ = std::make_unique<SynthSlider>("sub_level");
  addSlider(level_.get());
  level_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  pan_ = std::make_unique<SynthSlider>("sub_pan");
  addSlider(pan_.get());
  pan_->setBipolar();
  pan_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  on_ = std::make_unique<SynthButton>("sub_on");
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kSample);
}

SubSection::~SubSection() = default;

Rectangle<int> SubSection::waveformBounds() const {
  int widget_margin = findValue(Skin::kWidgetMargin);
  int bottom = waveform_->getY() - widget_margin;
  return Rectangle<int>(waveform_->getX(), widget_margin, waveform_->getWidth(), bottom - widget_margin);
}

void SubSection::paintWaveform(Graphics& g) {
  static constexpr int kResolution = 64;
  static constexpr float kVerticalPadding = 0.2f;

  Rectangle<float> bounds = waveformBounds().toFloat();
  g.setColour(findColour(Skin::kWidgetBackground, true));
  g.fillRoundedRectangle(bounds, findValue(Skin::kWidgetRoundedCorner));

  int waveform = (int)waveform_->getValue();
  float center = bounds.getCentreY();
  float amplitude = bounds.getHeight() * (0.5f - kVerticalPadding);
  Path path;
  for (int i = 0; i <= kResolution; ++i) {
    float t = i / (float)kResolution;
    float x = bounds.getX() + t * bounds.getWidth();
    float y = center - amplitude * waveformValue(waveform, t);
    if (i == 0)
      path.startNewSubPath(x, y);
    else
      path.lineTo(x, y);
  }

  Colour line = findColour(isActive() ? Skin::kWidgetPrimary1 : Skin::kWidgetPrimaryDisabled, true);
  g.setColour(line);
  g.strokePath(path, PathStrokeType(findValue(Skin::kWidgetLineWidth) * 0.5f, PathStrokeType::curved,
                                    PathStrokeType::rounded));
}

void SubSection::paintBackground(Graphics& g) {
  if (getWidth() == 0)
    return;

  paintContainer(g);
  paintHeadingText(g);
  paintKnobShadows(g);

  int widget_margin = findValue(Skin::kWidgetMargin);
  int label_height = findValue(Skin::kLabelBackgroundHeight);
  int pitch_x = getTitleWidth() + widget_margin;
  int pitch_width = transpose_->getRight() > 0 ? tune_->getRight() - transpose_->getX() : 0;
  int text_component_height = destination_->getY() - 2 * widget_margin;
  paintJointControl(g, pitch_x, widget_margin, pitch_width, text_component_height, "");

  setLabelFont(g);
  g.drawText("PITCH", pitch_x, widget_margin, pitch_width, label_height, Justification::centred, false);
  drawLabelForComponent(g, TRANS("LEVEL"), level_.get());
  drawLabelForComponent(g, TRANS("PAN"), pan_.get());

  float label_rounding = findValue(Skin::kLabelBackgroundRounding);
  g.setColour(findColour(Skin::kTextComponentBackground, true));
  g.fillRoundedRectangle(destination_->getBounds().toFloat(), label_rounding);
  g.fillRoundedRectangle(waveform_->getBounds().toFloat(), label_rounding);

  paintWaveform(g);
  paintChildrenBackgrounds(g);
  paintBorder(g);
}

void SubSection::resized() {
  SynthSection::resized();

  int title_width = getTitleWidth();
  int widget_margin = findValue(Skin::kWidgetMargin);
  int label_height = findValue(Skin::kLabelBackgroundHeight);
  int pitch_width = getWidth() * kPitchWidthPercent;
  int wave_width = getWidth() * kWaveWidthPercent;

  int pitch_x = title_width + widget_margin;
  int bottom_y = getHeight() - label_height - widget_margin;
  destination_->setBounds(pitch_x, bottom_y, pitch_width - widget_margin, label_height);
  placeJointControls(pitch_x, widget_margin, pitch_width - widget_margin, bottom_y - 2 * widget_margin,
                     transpose_.get(), tune_.get());

  int wave_x = title_width + pitch_width + widget_margin;
  waveform_->setBounds(wave_x, bottom_y, wave_width - widget_margin, label_height);

  int knobs_x = wave_x + wave_width;
  placeKnobsInArea(Rectangle<int>(knobs_x, 0, getWidth() - knobs_x, getHeight()), { level_.get(), pan_.get() });
}

void SubSection::sliderValueChanged(Slider* changed_slider) {
  SynthSection::sliderValueChanged(changed_slider);
  if (changed_slider == waveform_.get())
    repaintBackground();
}

void SubSection::setAllValues(vital::control_map& controls) {
  SynthSection::setAllValues(controls);
  repaintBackground();
}
