#include "PreviewAudio.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace
{
bool readAtSampleRate(const juce::File& file, double targetSampleRate,
                      juce::AudioBuffer<float>& destination, double& sourceSampleRate)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->sampleRate <= 0.0)
        return false;

    sourceSampleRate = reader->sampleRate;
    const auto channels = juce::jlimit(1, 2, static_cast<int>(reader->numChannels));
    juce::AudioBuffer<float> source(channels, static_cast<int>(reader->lengthInSamples) + 8);
    source.clear();
    reader->read(&source, 0, static_cast<int>(reader->lengthInSamples), 0, true, channels > 1);

    if (channels == 1)
    {
        source.setSize(2, source.getNumSamples(), true, true, false);
        source.copyFrom(1, 0, source, 0, 0, source.getNumSamples());
    }

    const auto outputSamples = juce::jmax(
        1, static_cast<int>(std::llround(static_cast<double>(reader->lengthInSamples)
                                         * targetSampleRate / reader->sampleRate)));
    destination.setSize(2, outputSamples, false, true, false);
    if (std::abs(reader->sampleRate - targetSampleRate) < 0.5)
    {
        for (int channel = 0; channel < 2; ++channel)
            destination.copyFrom(channel, 0, source, channel, 0, outputSamples);
        return true;
    }

    const auto speedRatio = reader->sampleRate / targetSampleRate;
    for (int channel = 0; channel < 2; ++channel)
    {
        juce::LagrangeInterpolator interpolator;
        interpolator.process(speedRatio, source.getReadPointer(channel),
                             destination.getWritePointer(channel), outputSamples);
    }
    return true;
}

float rms(const juce::AudioBuffer<float>& audio)
{
    float total = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        total += audio.getRMSLevel(channel, 0, audio.getNumSamples());
    return audio.getNumChannels() > 0 ? total / static_cast<float>(audio.getNumChannels()) : 0.0f;
}

float peak(const juce::AudioBuffer<float>& audio)
{
    float maximum = 0.0f;
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        maximum = juce::jmax(maximum, audio.getMagnitude(channel, 0, audio.getNumSamples()));
    return maximum;
}
}

bool loadPreviewClip(const juce::File& file,
                     double hostSampleRate,
                     const juce::File& loudnessReference,
                     PreviewClip& destination)
{
    juce::AudioBuffer<float> loaded;
    double sourceSampleRate = 0.0;
    if (!readAtSampleRate(file, hostSampleRate, loaded, sourceSampleRate))
        return false;

    float targetRms = rms(loaded);
    if (loudnessReference.existsAsFile())
    {
        juce::AudioBuffer<float> reference;
        double ignoredSampleRate = 0.0;
        if (readAtSampleRate(loudnessReference, hostSampleRate, reference, ignoredSampleRate))
            targetRms = rms(reference);
    }
    const auto loadedRms = rms(loaded);
    auto gain = loadedRms > 1.0e-8f ? targetRms / loadedRms : 1.0f;
    const auto loadedPeak = peak(loaded);
    if (loadedPeak > 0.0f)
        gain = juce::jmin(gain, 0.95f / loadedPeak);
    loaded.applyGain(gain);

    destination.audio = std::move(loaded);
    destination.sourceSampleRate = sourceSampleRate;
    destination.hostSampleRate = hostSampleRate;
    destination.appliedGain = gain;
    return true;
}

int renderPreviewReplacing(juce::AudioBuffer<float>& output,
                           const juce::AudioBuffer<float>& preview,
                           int previewPosition)
{
    output.clear();
    const auto available = juce::jmax(0, preview.getNumSamples() - previewPosition);
    const auto count = juce::jmin(output.getNumSamples(), available);
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
    {
        const auto sourceChannel = juce::jmin(channel, preview.getNumChannels() - 1);
        if (sourceChannel >= 0 && count > 0)
            output.copyFrom(channel, 0, preview, sourceChannel, previewPosition, count);
    }
    return previewPosition + count;
}
