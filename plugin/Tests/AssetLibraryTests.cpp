#include "AssetLibrary.h"

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
    const auto root = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("diverge-asset-library", {}, false);
    if (!root.createDirectory()) return fail("could not create asset test directory");
    const auto source = root.getChildFile("candidate.wav");
    if (!source.replaceWithData("stable-audio", 12))
        return fail("could not write asset fixture");

    AssetLibrary library(root.getChildFile("library"));
    const auto first = library.retain(source, "run:one", 3, "export", true);
    const auto second = library.retain(source, "run:one", 3, "branch", false);
    if (!first.isValid() || !second.isValid()) return fail("could not retain asset");
    if (first.contentId != second.contentId || first.object != second.object)
        return fail("identical audio was not content addressed");
    if (first.usageFile == first.object || second.usageFile != second.object)
        return fail("named export and object paths were not distinguished");
    if (first.usageFile.loadFileAsString() != "stable-audio")
        return fail("retained asset content changed");
    juce::StringArray events;
    library.root().getChildFile("events.jsonl").readLines(events);
    events.removeEmptyStrings();
    if (events.size() != 2) return fail("asset retention events were not append-only");

    root.deleteRecursively();
    return EXIT_SUCCESS;
}
