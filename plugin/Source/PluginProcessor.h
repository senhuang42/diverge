#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include "JobRunner.h"
#include "HostAudioContract.h"
#include "PreviewAudio.h"

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

    void beginCapture(int bars = 4);
    bool finishCapture(const juce::File& destination);
    bool isCapturing() const noexcept { return capturing.load(); }
    bool isCaptureActive() const noexcept { return captureArmed.load() || capturing.load(); }
    HostPositionFacts hostPosition() const noexcept;
    bool loadPreview(const juce::File& file, const juce::File& loudnessReference = {});
    void playPreview(bool alignToHost = true);
    void stopPreview();
    void seekPreview(double proportion);
    bool isPreviewPlaying() const noexcept { return previewPlaying.load(); }
    bool isPreviewAwaitingBeat() const;
    double previewProgress() const;
    juce::String previewPath() const;
    juce::ValueTree& state() noexcept { return pluginState; }
    JobRunner& generation() noexcept { return generationService; }

private:
    juce::AudioBuffer<float> captureBuffer;
    std::atomic<int> captureWritePosition { 0 };
    std::atomic<int> captureTargetSamples { 0 };
    std::atomic<int> captureBars { 4 };
    std::atomic<bool> captureArmed { false };
    std::atomic<bool> capturing { false };
    double currentSampleRate = 44100.0;
    std::atomic<bool> hostAvailable { false };
    std::atomic<bool> hostPlaying { false };
    std::atomic<double> hostBpm { 0.0 };
    std::atomic<bool> hostPpqAvailable { false };
    std::atomic<double> hostPpq { 0.0 };
    std::atomic<int> hostNumerator { 4 };
    std::atomic<int> hostDenominator { 4 };

    juce::AudioBuffer<float> previewBuffer;
    mutable juce::SpinLock previewLock;
    int previewPosition = 0;
    int previewLength = 0;
    bool previewAwaitingBeat = false;
    juce::String loadedPreviewPath;
    std::atomic<bool> previewPlaying { false };
    juce::ValueTree pluginState { "DivergeState" };
    JobRunner generationService;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DivergeAudioProcessor)
};
