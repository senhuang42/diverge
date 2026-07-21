#include "PreviewAudio.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
int fail(const char* message)
{
    std::cerr << message << std::endl;
    return EXIT_FAILURE;
}

bool writeSine(const juce::File& file, double sampleRate, float amplitude)
{
    juce::AudioBuffer<float> audio(1, static_cast<int>(sampleRate / 10.0));
    for (int sample = 0; sample < audio.getNumSamples(); ++sample)
        audio.setSample(0, sample, amplitude * static_cast<float>(
                                               std::sin(2.0 * juce::MathConstants<double>::pi
                                                        * 440.0 * sample / sampleRate)));
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr) return false;
    auto writer = format.createWriterFor(stream, juce::AudioFormatWriterOptions {}
                                                     .withSampleRate(sampleRate)
                                                     .withNumChannels(1)
                                                     .withBitsPerSample(32));
    return writer != nullptr && writer->writeFromAudioSampleBuffer(
                                    audio, 0, audio.getNumSamples());
}
}

int main()
{
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("diverge-preview-audio", {}, false);
    if (!root.createDirectory()) return fail("could not create preview test directory");
    const auto source = root.getChildFile("source.wav");
    const auto candidate = root.getChildFile("candidate.wav");
    if (!writeSine(source, 44100.0, 0.2f) || !writeSine(candidate, 22050.0, 0.05f))
        return fail("could not write preview fixtures");

    PreviewClip clip;
    if (!loadPreviewClip(candidate, 48000.0, source, clip))
        return fail("could not load preview clip");
    if (clip.audio.getNumChannels() != 2 || clip.audio.getNumSamples() != 4800)
        return fail("preview was not resampled to host format");
    const auto rms = clip.audio.getRMSLevel(0, 0, clip.audio.getNumSamples());
    if (rms < 0.13f || rms > 0.15f)
        return fail("preview was not loudness matched to source");

    juce::AudioBuffer<float> output(2, 256);
    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(output.getWritePointer(channel), 1.0f,
                                          output.getNumSamples());
    const auto next = renderPreviewReplacing(output, clip.audio, 100);
    if (next != 356) return fail("preview position did not advance by the block size");
    if (output.getMagnitude(0, 0, output.getNumSamples()) >= 0.5f)
        return fail("preview layered over live input instead of replacing it");

    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(output.getWritePointer(channel), 1.0f,
                                          output.getNumSamples());
    const auto partialNext = renderPreviewReplacing(output, clip.audio, 100, 64);
    if (partialNext != 292) return fail("partial-block preview advanced by the wrong amount");
    if (output.getSample(0, 63) != 1.0f || output.getMagnitude(0, 64, 192) >= 0.5f)
        return fail("partial-block preview did not preserve input before its boundary");

    root.deleteRecursively();
    return EXIT_SUCCESS;
}
