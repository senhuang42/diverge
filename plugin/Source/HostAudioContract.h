#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

bool isSupportedMainBusLayout(const juce::AudioChannelSet& input,
                              const juce::AudioChannelSet& output) noexcept;
int supportedCaptureChannels(int inputChannels) noexcept;
int captureCapacitySamples(double sampleRate) noexcept;
bool writeCapturedWav(const juce::File& destination,
                      const juce::AudioBuffer<float>& audio,
                      int samples,
                      double sampleRate);
