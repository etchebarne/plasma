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

#include "hyper_section.h"

#include "bar_renderer.h"
#include "hyper_module.h"
#include "skin.h"
#include "synth_button.h"
#include "synth_slider.h"
#include "text_look_and_feel.h"

// Shows the unison voices spread across the stereo field, wobbling at the Hyper rate by an amount
// that follows the detune. Driven from the knob values, so it needs no engine status outputs.
class HyperViewer : public BarRenderer {
  public:
    static constexpr int kNumBars = vital::HyperModule::kMaxVoices;

    HyperViewer() : BarRenderer(kNumBars, true), active_(true),
                    voices_(nullptr), rate_(nullptr), detune_(nullptr) {
      setBarWidth(0.12f);
      setScale(1.0f);
      setAdditiveBlending(true);
    }

    void setSliders(SynthSlider* voices, SynthSlider* rate, SynthSlider* detune) {
      voices_ = voices;
      rate_ = rate;
      detune_ = detune;
    }

    void render(OpenGlWrapper& open_gl, bool animate) override {
      static constexpr float kVoicePhaseOffsets[kNumBars] = { 0.0f, 0.43f, 0.71f, 0.17f, 0.89f, 0.31f, 0.57f };
      static constexpr float kVoiceRateSpread[kNumBars] = { 1.0f, 1.13f, 0.89f, 1.27f, 0.79f, 1.41f, 0.71f };
      static constexpr float kMaxWobble = 0.12f;
      static constexpr float kEdge = 0.8f;

      if (voices_ == nullptr)
        return;

      if (active_) {
        Colour color = findColour(Skin::kWidgetSecondary1, true);
        float alpha = color.getFloatAlpha();
        setColor(color.withAlpha(alpha + (1.0f - alpha) * alpha));
      }
      else
        setColor(findColour(Skin::kWidgetSecondaryDisabled, true));

      int num_voices = vital::utils::iclamp(voices_->getValue(), vital::HyperModule::kMinVoices, kNumBars);
      float rate = std::pow(2.0f, (float)rate_->getValue());
      float detune = detune_->getValue();
      double seconds = Time::getMillisecondCounterHiRes() * 0.001;

      for (int i = 0; i < num_voices; ++i) {
        float pan = kEdge * (2.0f * i / (num_voices - 1.0f) - 1.0f);
        double phase = seconds * rate * kVoiceRateSpread[i] + kVoicePhaseOffsets[i];
        float wobble = kMaxWobble * detune * std::sin(2.0 * vital::kPi * phase);
        float height = 0.75f - 0.25f * std::abs(pan);
        setX(i, pan + wobble);
        setY(i, height);
        setBottom(i, -height);
      }
      for (int i = num_voices; i < kNumBars; ++i)
        setX(i, -2.0f);

      BarRenderer::render(open_gl, animate);
      renderCorners(open_gl, animate);
    }

    void setActive(bool active) { active_ = active; }

  private:
    bool active_;
    SynthSlider* voices_;
    SynthSlider* rate_;
    SynthSlider* detune_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HyperViewer)
};

HyperSection::HyperSection(const String& name) : SynthSection(name) {
  static const double kVoicesDragSensitivity = 0.5;

  voices_ = std::make_unique<SynthSlider>("hyper_voices");
  addSlider(voices_.get());
  voices_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
  voices_->setLookAndFeel(TextLookAndFeel::instance());
  voices_->setSensitivity(kVoicesDragSensitivity);

  rate_ = std::make_unique<SynthSlider>("hyper_rate");
  addSlider(rate_.get());
  rate_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  detune_ = std::make_unique<SynthSlider>("hyper_detune");
  addSlider(detune_.get());
  detune_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  dry_wet_ = std::make_unique<SynthSlider>("hyper_dry_wet");
  addSlider(dry_wet_.get());
  dry_wet_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  dimension_size_ = std::make_unique<SynthSlider>("hyper_dimension_size");
  addSlider(dimension_size_.get());
  dimension_size_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  dimension_mix_ = std::make_unique<SynthSlider>("hyper_dimension_mix");
  addSlider(dimension_mix_.get());
  dimension_mix_->setSliderStyle(Slider::RotaryHorizontalVerticalDrag);

  viewer_ = std::make_unique<HyperViewer>();
  addOpenGlComponent(viewer_.get());
  viewer_->setSliders(voices_.get(), rate_.get(), detune_.get());

  on_ = std::make_unique<SynthButton>("hyper_on");
  addButton(on_.get());
  setActivator(on_.get());
  setSkinOverride(Skin::kChorus);
}

HyperSection::~HyperSection() = default;

void HyperSection::paintBackground(Graphics& g) {
  SynthSection::paintBackground(g);

  drawTextComponentBackground(g, voices_->getBounds(), true);

  setLabelFont(g);
  drawLabelForComponent(g, TRANS("UNISON"), voices_.get(), true);
  drawLabelForComponent(g, TRANS("RATE"), rate_.get());
  drawLabelForComponent(g, TRANS("DETUNE"), detune_.get());
  drawLabelForComponent(g, TRANS("MIX"), dry_wet_.get());
  drawLabelForComponent(g, TRANS("DIM SIZE"), dimension_size_.get());
  drawLabelForComponent(g, TRANS("DIM MIX"), dimension_mix_.get());
}

void HyperSection::resized() {
  int widget_margin = findValue(Skin::kWidgetMargin);
  int title_width = getTitleWidth();
  int section_height = getKnobSectionHeight();

  Rectangle<int> bounds = getLocalBounds().withLeft(title_width);
  Rectangle<int> hyper_area = getDividedAreaBuffered(bounds, 3, 0, widget_margin);
  Rectangle<int> dimension_area = getDividedAreaBuffered(bounds, 3, 2, widget_margin);

  placeKnobsInArea(Rectangle<int>(hyper_area.getX(), 0, hyper_area.getWidth(), section_height),
                   { voices_.get(), rate_.get() });
  voices_->setBounds(voices_->getBounds().withTop(widget_margin));

  int row_2_y = section_height - widget_margin;
  placeKnobsInArea(Rectangle<int>(hyper_area.getX(), row_2_y, hyper_area.getWidth(), section_height),
                   { detune_.get(), dry_wet_.get() });

  int dimension_y = (getHeight() - section_height) / 2;
  placeKnobsInArea(Rectangle<int>(dimension_area.getX(), dimension_y, dimension_area.getWidth(), section_height),
                   { dimension_size_.get(), dimension_mix_.get() });

  int viewer_x = hyper_area.getRight() + widget_margin;
  int viewer_width = dimension_area.getX() - widget_margin - viewer_x;
  viewer_->setBounds(viewer_x, widget_margin, viewer_width, getHeight() - 2 * widget_margin);

  SynthSection::resized();
}

void HyperSection::setActive(bool active) {
  SynthSection::setActive(active);
  viewer_->setActive(active);
}
