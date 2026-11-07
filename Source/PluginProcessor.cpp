// Copyright (C) 2026 Spectra
//
// This file is part of SpectraDrive, licensed under the GNU Affero General Public
// License v3.0 (AGPLv3). See the LICENSE file in the project root for details.

#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::NormalisableRange<float> skewedRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::AudioParameterFloatAttributes withText (std::function<juce::String (float)> fn)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [fn] (float v, int) { return fn (v); });
    }

    juce::String percent (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
}

SpectraDriveProcessor::SpectraDriveProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SpectraDrive", createLayout())
{
    mode      = apvts.getRawParameterValue (ParamIDs::mode);
    quality   = apvts.getRawParameterValue (ParamIDs::quality);
    drive     = apvts.getRawParameterValue (ParamIDs::drive);
    even      = apvts.getRawParameterValue (ParamIDs::even);
    cleanLows = apvts.getRawParameterValue (ParamIDs::cleanLows);
    tone      = apvts.getRawParameterValue (ParamIDs::tone);
    autoGain  = apvts.getRawParameterValue (ParamIDs::autoGain);
    mix       = apvts.getRawParameterValue (ParamIDs::mix);
    output    = apvts.getRawParameterValue (ParamIDs::output);

    setLatencySamples (spectra::latencyFor ((int) quality->load()));
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectraDriveProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::mode, 1 }, "Mode",
        StringArray { "Tube", "Tape", "Diode", "Hard", "Fold" }, spectra::Tube));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { ParamIDs::quality, 1 }, "Quality",
        StringArray { "1x (no latency)", "2x oversampling", "4x oversampling" }, 1));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, spectra::kMaxDriveDb, 0.1f), 12.0f,
        withText ([] (float v) { return "+" + String (v, 1) + " dB"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::even, 1 }, "Even", NormalisableRange<float> (0.0f, 1.0f), 0.2f,
        withText (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::cleanLows, 1 }, "Clean Lows",
        skewedRange (spectra::kCleanLowsOffHz, 800.0f, 150.0f), spectra::kCleanLowsOffHz,
        withText ([] (float v)
        {
            return v <= spectra::kCleanLowsOffHz + 0.5f ? String ("Off") : String (roundToInt (v)) + " Hz";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::tone, 1 }, "Tone", NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
        withText ([] (float v)
        {
            const int pc = roundToInt (v * 100.0f);
            return pc == 0 ? String ("Neutral") : pc < 0 ? "Dark " + String (-pc) : "Bright " + String (pc);
        })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::autoGain, 1 }, "Auto Gain", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f,
        withText (percent)));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        withText ([] (float v) { return String (v, 1) + " dB"; })));

    return layout;
}

bool SpectraDriveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

spectra::DriveParams SpectraDriveProcessor::readParams() const noexcept
{
    spectra::DriveParams p;
    p.mode        = (int) mode->load();
    p.quality     = (int) quality->load();
    p.driveDb     = drive->load();
    p.even        = even->load();
    p.cleanLowsHz = cleanLows->load();
    p.tone        = tone->load();
    p.autoGain    = autoGain->load() > 0.5f;
    p.mix         = mix->load();
    p.outGainDb   = output->load();
    return p;
}

void SpectraDriveProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    setLatencySamples (engine.getLatencySamples());
}

void SpectraDriveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (numOut, buffer.getNumChannels()),
                    buffer.getNumSamples());

    // Quality changes the oversampling filters' delay; tell the host so it can compensate.
    const int latency = engine.getLatencySamples();
    if (latency != getLatencySamples())
        setLatencySamples (latency);
}

juce::AudioProcessorEditor* SpectraDriveProcessor::createEditor()
{
    return new SpectraDriveEditor (*this);
}

void SpectraDriveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectraDriveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SpectraDriveProcessor();
}
