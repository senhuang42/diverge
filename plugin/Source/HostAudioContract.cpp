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

double barDurationSeconds(const HostPositionFacts& host, int bars) noexcept
{
    const auto safeBars = juce::jlimit(1, 8, bars);
    const auto safeBpm = host.bpm > 0.0 ? host.bpm : 120.0;
    const auto denominator = host.timeSignatureDenominator > 0
                                 ? host.timeSignatureDenominator
                                 : 4;
    const auto numerator = host.timeSignatureNumerator > 0 ? host.timeSignatureNumerator : 4;
    const auto quartersPerBar = static_cast<double>(numerator) * 4.0
                                / static_cast<double>(denominator);
    return static_cast<double>(safeBars) * quartersPerBar * 60.0 / safeBpm;
}

double sourceRegionDurationSeconds(const juce::File& source,
                                   const HostPositionFacts& host,
                                   int bars)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(source));
    if (reader == nullptr || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0)
        return 0.0;
    const auto fileDuration = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
    return juce::jmin(fileDuration, barDurationSeconds(host, bars));
}

BarCapturePlan planBarCapture(const HostPositionFacts& host, int bars,
                              int blockSamples) noexcept
{
    const auto safeRate = host.sampleRate > 0.0 ? host.sampleRate : 44100.0;
    const auto safeBpm = host.bpm > 0.0 ? host.bpm : 120.0;
    const auto denominator = host.timeSignatureDenominator > 0
                                 ? host.timeSignatureDenominator
                                 : 4;
    const auto numerator = host.timeSignatureNumerator > 0 ? host.timeSignatureNumerator : 4;
    const auto quartersPerBar = static_cast<double>(numerator) * 4.0
                                / static_cast<double>(denominator);
    BarCapturePlan result;
    result.targetSamples = juce::jmin(captureCapacitySamples(safeRate),
                                      static_cast<int>(std::llround(
                                          barDurationSeconds(host, bars) * safeRate)));
    if (!host.available)
    {
        result.startOffset = 0;
        return result;
    }
    if (!host.isPlaying)
    {
        result.waitingForTransport = true;
        return result;
    }
    if (host.bpm <= 0.0 || !host.ppqAvailable)
    {
        result.startOffset = 0;
        return result;
    }
    auto phase = std::fmod(host.ppqPosition, quartersPerBar);
    if (phase < 0.0) phase += quartersPerBar;
    const auto quartersToBoundary = phase < 1.0e-6 ? 0.0 : quartersPerBar - phase;
    const auto offset = static_cast<int>(std::llround(
        quartersToBoundary * 60.0 / safeBpm * safeRate));
    if (offset < blockSamples) result.startOffset = juce::jmax(0, offset);
    return result;
}

int planBeatAuditionStart(const HostPositionFacts& host, int blockSamples) noexcept
{
    if (!host.available || !host.isPlaying || host.bpm <= 0.0 || !host.ppqAvailable)
        return 0;
    const auto denominator = host.timeSignatureDenominator > 0
                                 ? host.timeSignatureDenominator
                                 : 4;
    const auto quartersPerBeat = 4.0 / static_cast<double>(denominator);
    auto phase = std::fmod(host.ppqPosition, quartersPerBeat);
    if (phase < 0.0) phase += quartersPerBeat;
    const auto quartersToBoundary = phase < 1.0e-6 ? 0.0 : quartersPerBeat - phase;
    const auto safeRate = host.sampleRate > 0.0 ? host.sampleRate : 44100.0;
    const auto offset = static_cast<int>(std::llround(
        quartersToBoundary * 60.0 / host.bpm * safeRate));
    return offset < blockSamples ? juce::jmax(0, offset) : -1;
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
