#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

class DivergeAudioProcessor final : public juce::AudioProcessor
{
public:
    DivergeAudioProcessor();
    ~DivergeAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    void beginCapture();
    bool finishCapture(const juce::File& destination);
    bool isCapturing() const noexcept { return capturing.load(); }
    bool loadPreview(const juce::File& file);
    void playPreview();
    juce::ValueTree& state() noexcept { return pluginState; }

private:
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int> captureWritePosition { 0 };
    std::atomic<bool> capturing { false };
    double currentSampleRate = 44100.0;

    juce::AudioBuffer<float> previewBuffer;
    juce::SpinLock previewLock;
    int previewPosition = 0;
    std::atomic<bool> previewPlaying { false };
    juce::ValueTree pluginState { "DivergeState" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DivergeAudioProcessor)
};
