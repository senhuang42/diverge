#include "AssetLibrary.h"

#include <juce_cryptography/juce_cryptography.h>

AssetLibrary::AssetLibrary(juce::File root)
    : rootDirectory(root == juce::File {}
                        ? juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                              .getChildFile("Diverge/Library")
                        : std::move(root))
{
}

RetainedAsset AssetLibrary::retain(const juce::File& source,
                                   const juce::String& runId,
                                   int candidateRank,
                                   const juce::String& reason,
                                   bool createNamedExport)
{
    if (!source.existsAsFile()) return {};
    const auto contentId = juce::SHA256(source).toHexString();
    const auto objectDirectory = rootDirectory.getChildFile("objects")
                                     .getChildFile(contentId.substring(0, 2));
    if (!objectDirectory.createDirectory()) return {};
    const auto extension = source.getFileExtension().isNotEmpty()
                               ? source.getFileExtension()
                               : juce::String(".wav");
    const auto object = objectDirectory.getChildFile(contentId + extension);
    if (!object.existsAsFile() && !source.copyFileTo(object)) return {};

    auto usageFile = object;
    if (createNamedExport)
    {
        const auto exportDirectory = rootDirectory.getChildFile("exports");
        if (!exportDirectory.createDirectory()) return {};
        const auto safeRun = juce::File::createLegalFileName(runId).substring(0, 32);
        const auto name = "Diverge_" + safeRun + "_" + juce::String(candidateRank).paddedLeft('0', 2)
                          + "_" + contentId.substring(0, 8) + extension;
        usageFile = exportDirectory.getChildFile(name);
        if (!usageFile.existsAsFile() && !object.copyFileTo(usageFile)) return {};
    }

    auto event = juce::JSON::parse("{}");
    auto* objectEvent = event.getDynamicObject();
    objectEvent->setProperty("event_id", juce::Uuid().toString());
    objectEvent->setProperty("timestamp", juce::Time::getCurrentTime().toISO8601(true));
    objectEvent->setProperty("type", "asset_retained");
    objectEvent->setProperty("content_id", contentId);
    objectEvent->setProperty("run_id", runId);
    objectEvent->setProperty("candidate_rank", candidateRank);
    objectEvent->setProperty("reason", reason);
    objectEvent->setProperty("object", object.getFullPathName());
    objectEvent->setProperty("usage_file", usageFile.getFullPathName());
    const auto events = rootDirectory.getChildFile("events.jsonl");
    if (!events.getParentDirectory().createDirectory()
        || !events.appendText(juce::JSON::toString(event, true) + "\n", false, false, "\n"))
        return {};
    return { contentId, object, usageFile };
}
