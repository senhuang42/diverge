#include "PluginProcessor.h"
#include "PluginEditor.h"

DivergeAudioProcessor::DivergeAudioProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
}

void DivergeAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    captureBuffer.setSize(2, static_cast<int>(sampleRate * 30.0), false, true, false);
    captureBuffer.clear();
}

void DivergeAudioProcessor::releaseResources() {}

bool DivergeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void DivergeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    if (capturing.load())
    {
        const auto start = captureWritePosition.load();
        const auto count = juce::jmin(buffer.getNumSamples(), captureBuffer.getNumSamples() - start);
        if (count > 0)
        {
            for (int channel = 0; channel < juce::jmin(2, buffer.getNumChannels()); ++channel)
                captureBuffer.copyFrom(channel, start, buffer, channel, 0, count);
            captureWritePosition += count;
        }
        if (captureWritePosition.load() >= captureBuffer.getNumSamples())
            capturing = false;
    }

    if (previewPlaying.load())
    {
        const juce::SpinLock::ScopedTryLockType lock(previewLock);
        if (lock.isLocked())
        {
            const auto count = juce::jmin(buffer.getNumSamples(), previewBuffer.getNumSamples() - previewPosition);
            for (int channel = 0; channel < juce::jmin(buffer.getNumChannels(), previewBuffer.getNumChannels()); ++channel)
                buffer.addFrom(channel, 0, previewBuffer, channel, previewPosition, count);
            previewPosition += count;
            if (previewPosition >= previewBuffer.getNumSamples())
                previewPlaying = false;
        }
    }
}

void DivergeAudioProcessor::beginCapture()
{
    captureBuffer.clear();
    captureWritePosition = 0;
    capturing = true;
}

bool DivergeAudioProcessor::finishCapture(const juce::File& destination)
{
    capturing = false;
    const auto samples = captureWritePosition.load();
    if (samples <= 0)
        return false;
    destination.getParentDirectory().createDirectory();
    juce::WavAudioFormat format;
    std::unique_ptr<juce::OutputStream> stream = destination.createOutputStream();
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions {}
                       .withSampleRate(currentSampleRate)
                       .withNumChannels(2)
                       .withBitsPerSample(32);
    auto writer = format.createWriterFor(stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer(captureBuffer, 0, samples);
}

bool DivergeAudioProcessor::loadPreview(const juce::File& file)
{
    juce::AudioFormatManager manager;
    manager.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(manager.createReaderFor(file));
    if (reader == nullptr)
        return false;
    juce::AudioBuffer<float> loaded(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&loaded, 0, loaded.getNumSamples(), 0, true, true);
    float peak = 0.0f;
    for (int channel = 0; channel < loaded.getNumChannels(); ++channel)
        peak = juce::jmax(peak, loaded.getMagnitude(channel, 0, loaded.getNumSamples()));
    if (peak > 0.0f)
        loaded.applyGain(juce::jmin(1.0f, 0.82f / peak));
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewBuffer = std::move(loaded);
    previewPosition = 0;
    previewLength = previewBuffer.getNumSamples();
    loadedPreviewPath = file.getFullPathName();
    return true;
}

void DivergeAudioProcessor::playPreview()
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewPlaying = previewBuffer.getNumSamples() > 0;
}

void DivergeAudioProcessor::stopPreview()
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewPlaying = false;
    previewPosition = 0;
}

void DivergeAudioProcessor::seekPreview(double proportion)
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewPosition = juce::jlimit(0, juce::jmax(0, previewLength - 1),
                                   static_cast<int>(proportion * static_cast<double>(previewLength)));
}

double DivergeAudioProcessor::previewProgress() const
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    return previewLength > 0 ? static_cast<double>(previewPosition) / static_cast<double>(previewLength) : 0.0;
}

juce::String DivergeAudioProcessor::previewPath() const
{
    const juce::SpinLock::ScopedLockType lock(previewLock);
    return loadedPreviewPath;
}

void DivergeAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    if (auto xml = pluginState.createXml())
        copyXmlToBinary(*xml, destination);
}

void DivergeAudioProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(pluginState.getType()))
            pluginState = juce::ValueTree::fromXml(*xml);
}

juce::AudioProcessorEditor* DivergeAudioProcessor::createEditor()
{
    return new DivergeAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DivergeAudioProcessor();
}
