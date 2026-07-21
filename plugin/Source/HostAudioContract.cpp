#include "HostAudioContract.h"

#include <cmath>

bool isSupportedMainBusLayout(const juce::AudioChannelSet& input,
                              const juce::AudioChannelSet& output) noexcept
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    return (input == mono && output == mono) || (input == stereo && output == stereo);
}

int supportedCaptureChannels(int inputChannels) noexcept
{
    return juce::jlimit(1, 2, inputChannels);
}

int captureCapacitySamples(double sampleRate) noexcept
{
    return juce::jmax(1, static_cast<int>(std::llround(sampleRate * 30.0)));
}

bool writeCapturedWav(const juce::File& destination,
                      const juce::AudioBuffer<float>& audio,
                      int samples,
                      double sampleRate)
{
    const auto count = juce::jlimit(0, audio.getNumSamples(), samples);
    const auto channels = supportedCaptureChannels(audio.getNumChannels());
    if (count == 0 || sampleRate <= 0.0) return false;
    if (!destination.getParentDirectory().createDirectory()) return false;
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();
    if (stream == nullptr) return false;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions {}
                    .withSampleRate(sampleRate)
                    .withNumChannels(channels)
                    .withBitsPerSample(32));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(audio, 0, count);
}
