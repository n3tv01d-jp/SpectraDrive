#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DriveEngine.h"

namespace ParamIDs
{
    inline constexpr const char* mode      = "mode";
    inline constexpr const char* quality   = "quality";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* even      = "even";
    inline constexpr const char* cleanLows = "cleanlows";
    inline constexpr const char* tone      = "tone";
    inline constexpr const char* autoGain  = "autogain";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
}

class SpectraDriveProcessor : public juce::AudioProcessor
{
public:
    SpectraDriveProcessor();
    ~SpectraDriveProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // UI feedback (read from the message thread).
    float getInputLevel() const noexcept  { return engine.getInputLevel(); }
    float getOutputLevel() const noexcept { return engine.getOutputLevel(); }
    float getDriveLevel() const noexcept  { return engine.getDriveLevel(); }
    float getAutoGain() const noexcept    { return engine.getAutoGain(); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    spectra::DriveParams readParams() const noexcept;

    spectra::DriveEngine engine;

    std::atomic<float>* mode = nullptr;
    std::atomic<float>* quality = nullptr;
    std::atomic<float>* drive = nullptr;
    std::atomic<float>* even = nullptr;
    std::atomic<float>* cleanLows = nullptr;
    std::atomic<float>* tone = nullptr;
    std::atomic<float>* autoGain = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* output = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectraDriveProcessor)
};
