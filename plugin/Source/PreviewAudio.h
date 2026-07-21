#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

struct PreviewClip
{
    juce::AudioBuffer<float> audio;
    double sourceSampleRate = 0.0;
    double hostSampleRate = 0.0;
    float appliedGain = 1.0f;
};

bool loadPreviewClip(const juce::File& file,
                     double hostSampleRate,
                     const juce::File& loudnessReference,
                     PreviewClip& destination);

int renderPreviewReplacing(juce::AudioBuffer<float>& output,
                           const juce::AudioBuffer<float>& preview,
                           int previewPosition);
