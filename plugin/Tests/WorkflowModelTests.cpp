#include "WorkflowModel.h"

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
    WorkflowModel original;
    original.choices[0].kept = true;
    original.choices[0].passed = true;
    original.choices[0].favorite = true;
    original.choices[0].exported = true;
    original.choices[0].branched = true;
    original.choices[1].passed = true;
    original.referenceMix = 73;
    original.refreshVisualDecisions();
    juce::ValueTree state("DivergeState");
    state.setProperty("preserveGroove", true, nullptr);
    state.setProperty("preserveMelody", true, nullptr);
    state.setProperty("preserveTimbre", true, nullptr);
    original.saveTo(state);
    if (state.hasProperty("preserveGroove") || state.hasProperty("preserveMelody")
        || state.hasProperty("preserveTimbre"))
        return fail("removed preserve settings remained in plugin state");

    WorkflowModel restored;
    restored.restoreFrom(state);
    if (restored.referenceMix != 73)
        return fail("reference mix did not survive state round trip");
    if (!restored.choices[0].kept || !restored.choices[0].passed
        || !restored.choices[0].favorite || !restored.choices[0].exported
        || !restored.choices[0].branched)
        return fail("independent choices did not survive state round trip");
    if (restored.decisions[0] != CandidateDecision::favorite
        || restored.decisions[1] != CandidateDecision::pass)
        return fail("visual decision was not derived from independent choices");
    juce::ValueTree legacy("DivergeState");
    legacy.setProperty("decision1", "exported", nullptr);
    WorkflowModel migrated;
    migrated.restoreFrom(legacy);
    if (!migrated.choices[0].exported || migrated.choices[0].kept)
        return fail("legacy decision did not migrate");

    const auto runDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                  .getNonexistentChildFile("diverge-short-run", {}, false);
    if (!runDirectory.createDirectory()) return fail("could not create run fixture");
    const auto manifest = R"json({
      "config": {
        "source": "/tmp/source.wav", "references": [["/tmp/ref.wav", 1.0]],
        "n_return": 8, "transform": 62, "reference_mix": 81, "spread": 35,
        "style_text_hint": "dusty and restrained"
      },
      "selection": {
        "requested_count": 8,
        "returned_count": 3,
        "shortfall": 5,
        "can_try_more": true
      },
      "candidates": [
        {"rank": 1, "path": "/tmp/one.wav",
         "explanation": "Melody retained; darker texture."},
        {"rank": 2, "path": "/tmp/two.wav"},
        {"rank": 3, "path": "/tmp/three.wav"}
      ]
    })json";
    if (!runDirectory.getChildFile("manifest.json").replaceWithText(manifest))
        return fail("could not write run fixture");
    const auto shortRun = RunModel::load(runDirectory);
    if (!shortRun.isValid() || shortRun.requestedCount != 8 || shortRun.returnedCount != 3
        || shortRun.shortfall != 5 || !shortRun.canTryMore)
        return fail("valid result shortfall was not loaded");
    if (shortRun.change != 62 || shortRun.referenceMix != 81 || shortRun.range != 35
        || shortRun.direction != "dusty and restrained" || shortRun.references.size() != 1)
        return fail("run brief was not restored from the manifest");
    if (shortRun.candidates[0].explanation != "Melody retained; darker texture.")
        return fail("measured candidate explanation was not loaded");
    const auto emptyManifest = R"json({
      "config": {"source": "/tmp/source.wav", "n_return": 8},
      "selection": {"requested_count": 8, "returned_count": 0, "shortfall": 8,
                    "can_try_more": true},
      "candidates": []
    })json";
    if (!runDirectory.getChildFile("manifest.json").replaceWithText(emptyManifest))
        return fail("could not write empty run fixture");
    const auto emptyRun = RunModel::load(runDirectory);
    if (!emptyRun.isValid() || emptyRun.returnedCount != 0 || emptyRun.shortfall != 8
        || !emptyRun.canTryMore || !emptyRun.candidates.empty())
        return fail("zero-result valid subset was treated as a corrupt run");
    runDirectory.deleteRecursively();
    return EXIT_SUCCESS;
}
