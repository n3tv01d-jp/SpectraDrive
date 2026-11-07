#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    namespace Colours
    {
        const juce::Colour bg       { 0xff101218 };
        const juce::Colour panel    { 0xff181b24 };
        const juce::Colour outline  { 0xff2a2f3d };
        const juce::Colour text     { 0xffd8dce6 };
        const juce::Colour dim      { 0xff7d8496 };
        const juce::Colour accent   { 0xffffa04d };   // amber  - odd harmonics
        const juce::Colour accent2  { 0xffff4f7a };   // pink   - even harmonics
        const juce::Colour hot      { 0xffff3b3b };   // clipping
    }

    int openEditors = 0;   // message thread only

    float gainToDb (float g) { return 20.0f * std::log10 (juce::jmax (g, 1.0e-6f)); }

    void fillPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::panel);
        g.fillRoundedRectangle (r, 8.0f);
    }

    void drawCaption (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s,
                      juce::Justification j = juce::Justification::topLeft)
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (s, r, j);
    }
}

//==============================================================================
DriveLookAndFeel::DriveLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, Colours::dim);
    setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    setColour (juce::ComboBox::outlineColourId, Colours::outline);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::arrowColourId, Colours::accent);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour (juce::ToggleButton::textColourId, Colours::text);

    regular = juce::Typeface::createSystemTypefaceFor (BinaryData::DejaVuSans_ttf,
                                                       (size_t) BinaryData::DejaVuSans_ttfSize);
    bold    = juce::Typeface::createSystemTypefaceFor (BinaryData::DejaVuSansBold_ttf,
                                                       (size_t) BinaryData::DejaVuSansBold_ttfSize);
}

juce::Typeface::Ptr DriveLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    // The UI only uses the default font family, so every request maps to the embedded fonts.
    if (auto tf = font.isBold() ? bold : regular)
        return tf;

    return LookAndFeel_V4::getTypefaceForFont (font);   // embedded font failed to load
}

void DriveLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = 3.5f;

    juce::Path bgArc;
    bgArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours::outline);
    g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path valArc;
    valArc.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setGradientFill (juce::ColourGradient (Colours::accent, bounds.getBottomLeft(),
                                             Colours::accent2, bounds.getTopRight(), false));
    g.strokePath (valArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = radius - track * 2.8f;
    g.setColour (Colours::panel.brighter (0.12f));
    g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -knobR + 2.0f, 3.0f, knobR * 0.55f, 1.5f);
    g.setColour (Colours::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void DriveLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool highlighted, bool)
{
    const auto r = b.getLocalBounds().toFloat();
    const bool on = b.getToggleState();

    auto pill = r.withWidth (38.0f).withSizeKeepingCentre (38.0f, 20.0f).withX (r.getX() + 2.0f);
    g.setColour (on ? Colours::accent.withAlpha (0.85f) : Colours::outline);
    g.fillRoundedRectangle (pill, 10.0f);
    if (highlighted)
    {
        g.setColour (Colours::text.withAlpha (0.25f));
        g.drawRoundedRectangle (pill, 10.0f, 1.0f);
    }

    const float d = 14.0f;
    const float cx = on ? pill.getRight() - d * 0.5f - 3.0f : pill.getX() + d * 0.5f + 3.0f;
    g.setColour (on ? Colours::bg : Colours::dim);
    g.fillEllipse (cx - d * 0.5f, pill.getCentreY() - d * 0.5f, d, d);

    g.setColour (on ? Colours::text : Colours::dim);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText (b.getButtonText(), r.withTrimmedLeft (pill.getWidth() + 10.0f), juce::Justification::centredLeft);
}

//==============================================================================
void CurveView::update (const CurveState& s, float inputLevel)
{
    const float lv = juce::jlimit (0.0f, 1.0f, inputLevel);
    if (s != state || std::abs (lv - level) > 0.003f)
    {
        state = s;
        level = lv;
        repaint();
    }
}

void CurveView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    fillPanel (g, b);
    drawCaption (g, b.reduced (10.0f, 8.0f), "TRANSFER");

    const float side = juce::jmin (b.getWidth() - 28.0f, b.getHeight() - 34.0f);
    const auto plot = juce::Rectangle<float> (side, side).withCentre ({ b.getCentreX(), b.getCentreY() + 8.0f });
    constexpr float yRange = 1.2f;

    auto toPt = [&] (float x, float y)
    {
        return juce::Point<float> (plot.getX() + (x + 1.0f) * 0.5f * plot.getWidth(),
                                   plot.getCentreY() - juce::jlimit (-yRange, yRange, y) / yRange * plot.getHeight() * 0.5f);
    };

    // Grid: axes, unity line and the +/-1 ceiling.
    g.setColour (Colours::outline);
    g.drawRect (plot, 1.0f);
    g.drawHorizontalLine ((int) plot.getCentreY(), plot.getX(), plot.getRight());
    g.drawVerticalLine ((int) plot.getCentreX(), plot.getY(), plot.getBottom());
    for (float yv : { -1.0f, 1.0f })
    {
        const float yy = toPt (0.0f, yv).y;
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine ({ plot.getX(), yy, plot.getRight(), yy }, dashes, 2, 1.0f);
    }
    {
        const float dashes[] = { 4.0f, 4.0f };
        g.setColour (Colours::dim.withAlpha (0.35f));
        g.drawDashedLine ({ toPt (-1.0f, -1.0f), toPt (1.0f, 1.0f) }, dashes, 2, 1.0f);
    }

    // Curve
    juce::Path path;
    constexpr int kPoints = 480;
    for (int i = 0; i <= kPoints; ++i)
    {
        const float x = -1.0f + 2.0f * (float) i / (float) kPoints;
        const auto pt = toPt (x, spectra::transfer (state.mode, x, state.driveGain, state.bias));
        if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
    }
    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());
    g.setGradientFill (juce::ColourGradient (Colours::accent, plot.getBottomLeft(),
                                             Colours::accent2, plot.getTopRight(), false));
    g.strokePath (path, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.restoreState();

    // Where the signal currently sits on the curve.
    if (level > 0.002f)
    {
        for (float x : { -level, level })
        {
            const auto pt = toPt (x, spectra::transfer (state.mode, x, state.driveGain, state.bias));
            g.setColour (Colours::text.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (pt));
            g.setColour (Colours::text);
            g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (pt));
        }
    }
}

//==============================================================================
void HarmonicsView::update (const CurveState& s)
{
    if (s == state)
        return;
    state = s;

    // One exact period of a -6 dBFS sine through the static curve; DFT at H1..H10.
    constexpr int N = 1024;
    constexpr double twoPi = 6.283185307179586;
    std::array<double, kNumHarmonics + 1> re {}, im {};
    for (int n = 0; n < N; ++n)
    {
        const double ph = twoPi * n / N;
        const double y = spectra::transfer (s.mode, 0.5f * (float) std::sin (ph), s.driveGain, s.bias);
        for (int h = 1; h <= kNumHarmonics; ++h)
        {
            re[(size_t) h] += y * std::cos (ph * h);
            im[(size_t) h] += y * std::sin (ph * h);
        }
    }

    std::array<double, kNumHarmonics + 1> amp {};
    double strongest = 1e-12, distortion = 0.0;
    for (int h = 1; h <= kNumHarmonics; ++h)
    {
        amp[(size_t) h] = std::sqrt (re[(size_t) h] * re[(size_t) h] + im[(size_t) h] * im[(size_t) h]) * 2.0 / N;
        strongest = std::max (strongest, amp[(size_t) h]);
        if (h > 1) distortion += amp[(size_t) h] * amp[(size_t) h];
    }
    for (int h = 1; h <= kNumHarmonics; ++h)
        db[(size_t) h] = (float) juce::jmax (-120.0, 20.0 * std::log10 (amp[(size_t) h] / strongest + 1e-12));

    thd = (float) juce::jmin (999.0, 100.0 * std::sqrt (distortion) / std::max (amp[1], 1e-12));
    repaint();
}

void HarmonicsView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    fillPanel (g, b);
    auto inner = b.reduced (10.0f, 8.0f);
    auto top = inner.removeFromTop (16.0f);
    drawCaption (g, top, "HARMONICS  (-6 dBFS SINE)");

    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.setColour (Colours::text);
    g.drawText ("THD " + juce::String (thd, thd < 10.0f ? 2 : 1) + " %", top, juce::Justification::centredRight);

    auto legend = inner.removeFromBottom (14.0f);
    inner.removeFromBottom (16.0f);   // harmonic numbers
    auto plot = inner.withTrimmedLeft (30.0f).withTrimmedTop (6.0f);

    constexpr float floorDb = -90.0f;
    auto yFor = [&] (float d) { return plot.getY() + (juce::jlimit (floorDb, 0.0f, d) / floorDb) * plot.getHeight(); };

    g.setFont (juce::FontOptions (10.0f));
    for (int d = 0; d >= (int) floorDb; d -= 30)
    {
        const float yy = yFor ((float) d);
        g.setColour (Colours::outline);
        g.drawHorizontalLine ((int) yy, plot.getX(), plot.getRight());
        g.setColour (Colours::dim);
        g.drawText (juce::String (d), juce::Rectangle<float> (inner.getX(), yy - 7.0f, 26.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    const float slot = plot.getWidth() / (float) kNumHarmonics;
    for (int h = 1; h <= kNumHarmonics; ++h)
    {
        const float x = plot.getX() + slot * (float) (h - 1);
        const auto col = h == 1 ? Colours::text.withAlpha (0.8f) : (h % 2 == 0 ? Colours::accent2 : Colours::accent);
        const float yy = yFor (db[(size_t) h]);
        auto bar = juce::Rectangle<float> (x + slot * 0.2f, yy, slot * 0.6f, plot.getBottom() - yy);
        if (bar.getHeight() > 0.5f)
        {
            g.setGradientFill (juce::ColourGradient (col, bar.getTopLeft(), col.withAlpha (0.25f), bar.getBottomLeft(), false));
            g.fillRoundedRectangle (bar, 2.5f);
        }
        g.setColour (Colours::dim);
        g.drawText (juce::String (h), juce::Rectangle<float> (x, plot.getBottom() + 2.0f, slot, 14.0f),
                    juce::Justification::centred);
    }

    // Legend
    auto item = [&] (juce::Colour c, const juce::String& s)
    {
        g.setColour (c);
        g.fillRoundedRectangle (legend.removeFromLeft (10.0f).withSizeKeepingCentre (10.0f, 10.0f), 2.0f);
        legend.removeFromLeft (5.0f);
        g.setColour (Colours::dim);
        g.drawText (s, legend.removeFromLeft (70.0f), juce::Justification::centredLeft);
    };
    legend.removeFromLeft (30.0f);
    item (Colours::accent,  "odd");
    item (Colours::accent2, "even");
}

//==============================================================================
void LevelMeters::update (float in, float out, float autoGain, bool autoGainOn)
{
    if (std::abs (in - inLevel) > 1e-4f || std::abs (out - outLevel) > 1e-4f
        || std::abs (autoGain - ag) > 1e-3f || autoGainOn != agOn)
    {
        inLevel = in; outLevel = out; ag = autoGain; agOn = autoGainOn;
        repaint();
    }
}

void LevelMeters::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    fillPanel (g, b);
    auto inner = b.reduced (10.0f, 8.0f);
    drawCaption (g, inner.removeFromTop (16.0f), "LEVEL", juce::Justification::centredLeft);

    auto agRow = inner.removeFromBottom (16.0f);
    g.setFont (juce::FontOptions (11.0f));
    g.setColour (agOn ? Colours::text : Colours::dim);
    g.drawText (agOn ? "AG " + juce::String (gainToDb (ag), 1) + " dB" : juce::String ("AG off"),
                agRow, juce::Justification::centred);

    auto labels = inner.removeFromBottom (16.0f);
    inner.removeFromTop (4.0f);

    constexpr float lo = -48.0f, hi = 6.0f;
    auto yFor = [&] (float d) { return inner.getBottom() - (juce::jlimit (lo, hi, d) - lo) / (hi - lo) * inner.getHeight(); };

    const float w = inner.getWidth() / 2.0f;
    int idx = 0;
    for (float lvl : { inLevel, outLevel })
    {
        auto col = juce::Rectangle<float> (inner.getX() + w * (float) idx, inner.getY(), w, inner.getHeight()).reduced (7.0f, 0.0f);
        g.setColour (Colours::bg);
        g.fillRoundedRectangle (col, 3.0f);

        const float d = gainToDb (lvl);
        if (d > lo)
        {
            auto fill = col.withTop (yFor (d));
            g.setGradientFill (juce::ColourGradient (d > 0.0f ? Colours::hot : Colours::accent2, col.getTopLeft(),
                                                     Colours::accent, col.getBottomLeft(), false));
            g.fillRoundedRectangle (fill, 3.0f);
        }
        g.setColour (Colours::outline.brighter (0.3f));
        g.drawHorizontalLine ((int) yFor (0.0f), col.getX(), col.getRight());

        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (idx == 0 ? "IN" : "OUT", labels.withX (col.getX() - 7.0f).withWidth (w), juce::Justification::centred);
        ++idx;
    }
}

//==============================================================================
ModeSelector::ModeSelector (juce::RangedAudioParameter& p) : param (p), names (p.getAllValueStrings()) {}

void ModeSelector::refresh()
{
    const int idx = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
    if (idx != current)
    {
        current = idx;
        repaint();
    }
}

void ModeSelector::mouseDown (const juce::MouseEvent& e)
{
    if (names.isEmpty()) return;
    const int idx = juce::jlimit (0, names.size() - 1, (int) (e.position.x / (float) getWidth() * (float) names.size()));
    param.beginChangeGesture();
    param.setValueNotifyingHost (param.convertTo0to1 ((float) idx));
    param.endChangeGesture();
    refresh();
}

void ModeSelector::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (b.reduced (0.5f), 6.0f, 1.0f);

    const float w = b.getWidth() / (float) juce::jmax (1, names.size());
    for (int i = 0; i < names.size(); ++i)
    {
        auto seg = juce::Rectangle<float> (b.getX() + w * (float) i, b.getY(), w, b.getHeight()).reduced (3.0f);
        const bool sel = i == current;
        if (sel)
        {
            g.setGradientFill (juce::ColourGradient (Colours::accent, seg.getTopLeft(), Colours::accent2, seg.getBottomRight(), false));
            g.fillRoundedRectangle (seg, 4.0f);
        }
        g.setColour (sel ? Colours::bg : Colours::dim);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText (names[i], seg, juce::Justification::centred);
    }
}

//==============================================================================
SpectraDriveEditor::SpectraDriveEditor (SpectraDriveProcessor& p)
    : AudioProcessorEditor (&p), proc (p), modeSelector (*p.apvts.getParameter (ParamIDs::mode))
{
    // Font lookups go through the default LookAndFeel (see DriveLookAndFeel). Fonts resolved
    // before this point may be cached with the system typeface, so clear that cache.
    ++openEditors;
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf.get());
    juce::Typeface::clearTypefaceCache();
    setLookAndFeel (&lnf.get());

    addAndMakeVisible (curve);
    addAndMakeVisible (harmonics);
    addAndMakeVisible (meters);
    addAndMakeVisible (modeSelector);

    for (auto* l : { &modeLabel, &qualityLabel })
    {
        l->setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (*l);
    }
    modeLabel.setText ("Mode", juce::dontSendNotification);
    qualityLabel.setText ("Quality", juce::dontSendNotification);

    quality.addItemList (proc.apvts.getParameter (ParamIDs::quality)->getAllValueStrings(), 1);
    addAndMakeVisible (quality);
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        proc.apvts, ParamIDs::quality, quality);

    addAndMakeVisible (autoGain);
    autoGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, ParamIDs::autoGain, autoGain);

    addKnob (knobs[0], ParamIDs::drive,     "Drive");
    addKnob (knobs[1], ParamIDs::even,      "Even");
    addKnob (knobs[2], ParamIDs::cleanLows, "Clean Lows");
    addKnob (knobs[3], ParamIDs::tone,      "Tone");
    addKnob (knobs[4], ParamIDs::mix,       "Mix");
    addKnob (knobs[5], ParamIDs::output,    "Output");

    latencyLabel.setJustificationType (juce::Justification::centredRight);
    latencyLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (latencyLabel);

    setSize (820, 470);
    timerCallback();
    startTimerHz (30);
}

SpectraDriveEditor::~SpectraDriveEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);

    // Last open editor: hand the default LookAndFeel back to JUCE before ours is destroyed.
    if (--openEditors == 0 && &juce::LookAndFeel::getDefaultLookAndFeel() == &lnf.get())
    {
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        juce::Typeface::clearTypefaceCache();
    }
}

void SpectraDriveEditor::addKnob (Knob& k, const juce::String& paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
    k.slider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k.label);
}

CurveState SpectraDriveEditor::currentCurve() const
{
    CurveState s;
    s.mode      = (int) proc.apvts.getRawParameterValue (ParamIDs::mode)->load();
    s.driveGain = juce::Decibels::decibelsToGain (proc.apvts.getRawParameterValue (ParamIDs::drive)->load());
    s.bias      = spectra::biasFor (proc.apvts.getRawParameterValue (ParamIDs::even)->load());
    return s;
}

// Under Wine the window uses JUCE's software renderer (see cmake/PatchJuceWine.cmake).
void SpectraDriveEditor::timerCallback()
{
    modeSelector.refresh();

    const auto cs = currentCurve();
    curve.update (cs, proc.getDriveLevel() / cs.driveGain);
    harmonics.update (cs);

    const bool agOn = proc.apvts.getRawParameterValue (ParamIDs::autoGain)->load() > 0.5f;
    meters.update (proc.getInputLevel(), proc.getOutputLevel(), proc.getAutoGain(), agOn);

    const int lat = proc.getLatencySamples();
    latencyLabel.setText (lat == 0 ? juce::String ("zero latency") : "latency " + juce::String (lat) + " samples",
                          juce::dontSendNotification);

   #if JUCE_WINDOWS
    // JUCE 8 flushes repaints on the display's vblank signal, which Wine may not
    // deliver. Flushing them here too keeps the UI redrawing either way.
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();
   #endif
}

void SpectraDriveEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    auto header = getLocalBounds().removeFromTop (48).toFloat();
    const juce::Font titleFont (juce::FontOptions (24.0f, juce::Font::bold));
    const float titleWidth = juce::GlyphArrangement::getStringWidth (titleFont, "SpectraDrive");
    g.setFont (titleFont);
    g.setGradientFill (juce::ColourGradient (Colours::accent, header.getX() + 20.0f, 0.0f,
                                             Colours::accent2, header.getX() + 20.0f + titleWidth, 0.0f, false));
    g.drawText ("SpectraDrive", header.reduced (20.0f, 0.0f), juce::Justification::centredLeft);

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("harmonic distortion", header.reduced (20.0f, 0.0f).withTrimmedLeft (titleWidth + 12.0f),
                juce::Justification::centredLeft);

    // Knob panel
    fillPanel (g, juce::Rectangle<float> (12.0f, (float) getHeight() - 160.0f, (float) getWidth() - 24.0f, 148.0f));
}

void SpectraDriveEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    auto header = area.removeFromTop (36);
    latencyLabel.setBounds (header.removeFromRight (220).reduced (8, 0));

    // Displays
    auto displays = area.removeFromTop (190);
    curve.setBounds (displays.removeFromLeft (210));
    displays.removeFromLeft (10);
    meters.setBounds (displays.removeFromRight (100));
    displays.removeFromRight (10);
    harmonics.setBounds (displays);

    // Mode / quality / auto gain
    area.removeFromTop (10);
    auto row = area.removeFromTop (48).reduced (4, 0);
    {
        auto cell = row.removeFromLeft (390);
        modeLabel.setBounds (cell.removeFromTop (18));
        modeSelector.setBounds (cell.removeFromTop (28));
    }
    row.removeFromLeft (16);
    {
        auto cell = row.removeFromLeft (200);
        qualityLabel.setBounds (cell.removeFromTop (18));
        quality.setBounds (cell.removeFromTop (28));
    }
    row.removeFromLeft (20);
    autoGain.setBounds (row.withTrimmedTop (18).removeFromTop (28));

    // Knobs (inside the panel painted in paint())
    auto knobRow = getLocalBounds().removeFromBottom (160).withTrimmedBottom (12).reduced (16, 8);
    const int kw = knobRow.getWidth() / (int) knobs.size();
    for (auto& k : knobs)
    {
        auto cell = knobRow.removeFromLeft (kw);
        k.label.setBounds (cell.removeFromTop (18));
        k.slider.setBounds (cell);
    }
}
