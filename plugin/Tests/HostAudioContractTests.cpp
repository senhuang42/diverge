#include "HostAudioContract.h"

#include <cstdlib>
#include <iostream>

namespace
{
int fail(const char* message)
{
    std::cerr << message << std::endl;
    return EXIT_FAILURE;
}
}

int main()
{
    const auto mono = juce::AudioChannelSet::mono();
    const auto stereo = juce::AudioChannelSet::stereo();
    if (!isSupportedMainBusLayout(mono, mono) || !isSupportedMainBusLayout(stereo, stereo))
        return fail("mono or stereo matching layout was rejected");
    if (isSupportedMainBusLayout(mono, stereo) || isSupportedMainBusLayout(stereo, mono))
        return fail("mismatched input/output layout was accepted");
    for (const auto sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0 })
        if (captureCapacitySamples(sampleRate) != static_cast<int>(sampleRate * 30.0))
            return fail("capture capacity did not follow the host sample rate");

    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("diverge-host-audio", {}, false);
    if (!root.createDirectory()) return fail("could not create capture fixture directory");
    for (const auto channels : { 1, 2 })
    {
        juce::AudioBuffer<float> audio(channels, 256);
        for (int channel = 0; channel < channels; ++channel)
            juce::FloatVectorOperations::fill(audio.getWritePointer(channel),
                                              0.1f * static_cast<float>(channel + 1), 256);
        const auto file = root.getChildFile(juce::String(channels) + "ch.wav");
        if (!writeCapturedWav(file, audio, 200, 48000.0))
            return fail("could not write capture fixture");
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatReader> reader(
            format.createReaderFor(file.createInputStream().release(), true));
        if (reader == nullptr || static_cast<int>(reader->numChannels) != channels
            || reader->lengthInSamples != 200 || reader->sampleRate != 48000.0)
            return fail("captured WAV did not preserve host channels, length, and sample rate");
    }
    root.deleteRecursively();
    return EXIT_SUCCESS;
}
