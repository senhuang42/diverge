#pragma once

#include <juce_core/juce_core.h>

struct RetainedAsset
{
    juce::String contentId;
    juce::File object;
    juce::File usageFile;

    bool isValid() const noexcept { return contentId.isNotEmpty() && usageFile.existsAsFile(); }
};

class AssetLibrary
{
public:
    explicit AssetLibrary(juce::File rootDirectory = {});

    RetainedAsset retain(const juce::File& source,
                         const juce::String& runId,
                         int candidateRank,
                         const juce::String& reason,
                         bool createNamedExport);

    const juce::File& root() const noexcept { return rootDirectory; }

private:
    juce::File rootDirectory;
};
