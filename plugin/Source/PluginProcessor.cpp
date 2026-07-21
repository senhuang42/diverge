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
    captureBuffer.setSize(supportedCaptureChannels(getTotalNumInputChannels()),
                          captureCapacitySamples(sampleRate), false, true, false);
    captureBuffer.clear();
}

void DivergeAudioProcessor::releaseResources() {}

bool DivergeAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return isSupportedMainBusLayout(layouts.getMainInputChannelSet(),
                                    layouts.getMainOutputChannelSet());
}

void DivergeAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    HostPositionFacts host;
    host.sampleRate = currentSampleRate;
    host.inputChannels = getTotalNumInputChannels();
    if (auto* playHead = getPlayHead())
        if (const auto position = playHead->getPosition())
        {
            host.available = true;
            host.isPlaying = position->getIsPlaying();
            if (const auto bpm = position->getBpm()) host.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition())
            {
                host.ppqAvailable = true;
                host.ppqPosition = *ppq;
            }
            if (const auto signature = position->getTimeSignature())
            {
                host.timeSignatureNumerator = signature->numerator;
                host.timeSignatureDenominator = signature->denominator;
            }
        }
    hostAvailable = host.available;
    hostPlaying = host.isPlaying;
    hostBpm = host.bpm;
    hostPpqAvailable = host.ppqAvailable;
    hostPpq = host.ppqPosition;
    hostNumerator = host.timeSignatureNumerator;
    hostDenominator = host.timeSignatureDenominator;

    int sourceOffset = 0;
    if (captureArmed.load())
    {
        const auto plan = planBarCapture(host, captureBars.load(), buffer.getNumSamples());
        if (plan.startOffset >= 0)
        {
            sourceOffset = plan.startOffset;
            captureTargetSamples = plan.targetSamples;
            captureArmed = false;
            capturing = true;
        }
    }
    if (capturing.load())
    {
        const auto start = captureWritePosition.load();
        const auto remaining = juce::jmin(captureBuffer.getNumSamples(), captureTargetSamples.load())
                               - start;
        const auto count = juce::jmin(buffer.getNumSamples() - sourceOffset, remaining);
        if (count > 0)
        {
            for (int channel = 0;
                 channel < juce::jmin(captureBuffer.getNumChannels(), buffer.getNumChannels());
                 ++channel)
                captureBuffer.copyFrom(channel, start, buffer, channel, sourceOffset, count);
            captureWritePosition += count;
        }
        if (captureWritePosition.load() >= captureTargetSamples.load())
            capturing = false;
    }

    if (previewPlaying.load())
    {
        buffer.clear();
        const juce::SpinLock::ScopedTryLockType lock(previewLock);
        if (lock.isLocked())
        {
            previewPosition = renderPreviewReplacing(buffer, previewBuffer, previewPosition);
            if (previewPosition >= previewBuffer.getNumSamples())
                previewPlaying = false;
        }
    }
}

void DivergeAudioProcessor::beginCapture(int bars)
{
    captureBuffer.clear();
    captureWritePosition = 0;
    captureTargetSamples = 0;
    captureBars = juce::jlimit(1, 8, bars);
    capturing = false;
    captureArmed = true;
}

bool DivergeAudioProcessor::finishCapture(const juce::File& destination)
{
    captureArmed = false;
    capturing = false;
    const auto samples = captureWritePosition.load();
    if (samples <= 0)
        return false;
    return writeCapturedWav(destination, captureBuffer, samples, currentSampleRate);
}

HostPositionFacts DivergeAudioProcessor::hostPosition() const noexcept
{
    HostPositionFacts result;
    result.available = hostAvailable.load();
    result.isPlaying = hostPlaying.load();
    result.sampleRate = currentSampleRate;
    result.inputChannels = getTotalNumInputChannels();
    result.bpm = hostBpm.load();
    result.ppqAvailable = hostPpqAvailable.load();
    result.ppqPosition = hostPpq.load();
    result.timeSignatureNumerator = hostNumerator.load();
    result.timeSignatureDenominator = hostDenominator.load();
    return result;
}

bool DivergeAudioProcessor::loadPreview(const juce::File& file,
                                        const juce::File& loudnessReference)
{
    PreviewClip loaded;
    if (!loadPreviewClip(file, currentSampleRate, loudnessReference, loaded))
        return false;
    const juce::SpinLock::ScopedLockType lock(previewLock);
    previewBuffer = std::move(loaded.audio);
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
