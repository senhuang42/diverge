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
    original.refreshVisualDecisions();
    juce::ValueTree state("DivergeState");
    original.saveTo(state);

    WorkflowModel restored;
    restored.restoreFrom(state);
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
    return EXIT_SUCCESS;
}
