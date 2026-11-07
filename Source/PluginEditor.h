// Copyright (C) 2026 Spectra
//
// This file is part of SpectraDrive, licensed under the GNU Affero General Public
// License v3.0 (AGPLv3). See the LICENSE file in the project root for details.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

// Shared by every SpectraDrive editor in the process (via SharedResourcePointer), and
// installed as JUCE's default LookAndFeel while an editor is open. JUCE 8 resolves every
// font through the *default* LookAndFeel, so this is what makes all text - including combo
// box popups and slider text boxes - use the embedded fonts instead of system fonts.
class DriveLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DriveLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

private:
    juce::Typeface::Ptr regular, bold;
};

// Snapshot of the parameters that define the static curve.
struct CurveState
{
    int   mode = 0;
    float driveGain = 1.0f, bias = 0.0f;
    bool operator== (const CurveState& o) const { return mode == o.mode && juce::exactlyEqual (driveGain, o.driveGain) && juce::exactlyEqual (bias, o.bias); }
    bool operator!= (const CurveState& o) const { return ! (*this == o); }
};

// Transfer curve (input -> output) with a marker at the current signal level.
class CurveView : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void update (const CurveState&, float inputLevel);

private:
    CurveState state;
    float level = 0.0f;
};

// Harmonic profile of a -6 dBFS sine through the current curve (H1..H10).
class HarmonicsView : public juce::Component
{
public:
    static constexpr int kNumHarmonics = 10;
    void paint (juce::Graphics&) override;
    void update (const CurveState&);

private:
    CurveState state { -1, 0.0f, 0.0f };
    std::array<float, kNumHarmonics + 1> db {};   // relative to H1, index = harmonic number
    float thd = 0.0f;
};

class LevelMeters : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void update (float in, float out, float autoGain, bool autoGainOn);

private:
    float inLevel = 0.0f, outLevel = 0.0f, ag = 1.0f;
    bool agOn = false;
};

// Segmented buttons for the Mode parameter.
class ModeSelector : public juce::Component
{
public:
    explicit ModeSelector (juce::RangedAudioParameter&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();

private:
    juce::RangedAudioParameter& param;
    juce::StringArray names;
    int current = -1;
};

class SpectraDriveEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpectraDriveEditor (SpectraDriveProcessor&);
    ~SpectraDriveEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);
    CurveState currentCurve() const;

    SpectraDriveProcessor& proc;
    juce::SharedResourcePointer<DriveLookAndFeel> lnf;   // declared before the components that use it

    CurveView curve;
    HarmonicsView harmonics;
    LevelMeters meters;
    ModeSelector modeSelector;

    juce::Label modeLabel, qualityLabel;
    juce::ComboBox quality;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> qualityAttachment;
    juce::ToggleButton autoGain { "Auto Gain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoGainAttachment;

    std::array<Knob, 6> knobs;
    juce::Label latencyLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraDriveEditor)
};
