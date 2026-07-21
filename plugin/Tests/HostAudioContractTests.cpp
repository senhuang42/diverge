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

    HostPositionFacts host;
    host.sampleRate = 48000.0;
    auto plan = planBarCapture(host, 4, 512);
    if (plan.startOffset != 0 || plan.targetSamples != 384000)
        return fail("host-free capture did not start immediately at the requested bar length");
    host.available = true;
    plan = planBarCapture(host, 4, 512);
    if (!plan.waitingForTransport || plan.startOffset >= 0)
        return fail("stopped host capture was not armed for transport");
    host.isPlaying = true;
    host.bpm = 120.0;
    host.ppqAvailable = true;
    host.ppqPosition = 3.999;
    plan = planBarCapture(host, 2, 512);
    if (plan.startOffset != 24 || plan.targetSamples != 192000)
        return fail("capture did not align to the next host bar");
    host.ppqPosition = 3.0;
    if (planBarCapture(host, 2, 512).startOffset >= 0)
        return fail("capture began before the next host bar reached the block");
    host.ppqPosition = 4.0;
    if (planBarCapture(host, 2, 512).startOffset != 0)
        return fail("capture missed an exact host bar boundary");
    if (planBeatAuditionStart(host, 512) != 0)
        return fail("audition missed an exact host beat boundary");
    host.ppqPosition = 3.99;
    if (planBeatAuditionStart(host, 512) != 240)
        return fail("audition did not align inside the block at the next host beat");
    host.ppqPosition = 3.5;
    if (planBeatAuditionStart(host, 512) >= 0)
        return fail("audition began before the next host beat reached the block");
    host.isPlaying = false;
    if (planBeatAuditionStart(host, 512) != 0)
        return fail("stopped-host audition did not start immediately");
    host.isPlaying = true;
    host.timeSignatureDenominator = 8;
    host.ppqPosition = 3.5;
    if (planBeatAuditionStart(host, 512) != 0)
        return fail("audition ignored the host beat denominator");

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
