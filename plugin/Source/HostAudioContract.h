#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

struct HostPositionFacts
{
    bool available = false;
    bool isPlaying = false;
    double sampleRate = 44100.0;
    int inputChannels = 2;
    double bpm = 0.0;
    bool ppqAvailable = false;
    double ppqPosition = 0.0;
    int timeSignatureNumerator = 4;
    int timeSignatureDenominator = 4;
};

struct BarCapturePlan
{
    int startOffset = -1;
    int targetSamples = 0;
    bool waitingForTransport = false;
};

bool isSupportedMainBusLayout(const juce::AudioChannelSet& input,
                              const juce::AudioChannelSet& output) noexcept;
int supportedCaptureChannels(int inputChannels) noexcept;
int captureCapacitySamples(double sampleRate) noexcept;
double barDurationSeconds(const HostPositionFacts& host, int bars) noexcept;
double sourceRegionDurationSeconds(const juce::File& source,
                                   const HostPositionFacts& host,
                                   int bars);
BarCapturePlan planBarCapture(const HostPositionFacts& host, int bars,
                              int blockSamples) noexcept;
int planBeatAuditionStart(const HostPositionFacts& host, int blockSamples) noexcept;
bool writeCapturedWav(const juce::File& destination,
                      const juce::AudioBuffer<float>& audio,
                      int samples,
                      double sampleRate);
