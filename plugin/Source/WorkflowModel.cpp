#include "WorkflowModel.h"
#include <algorithm>

namespace
{
juce::String buildExplanation(const CandidateModel& candidate, int count)
{
    if (candidate.rank == count && count > 1)
        return "Wildcard · the widest departure";
    if (candidate.referenceFit >= 0.76)
        return "Closest to your direction";
    if (candidate.groove >= candidate.melody && candidate.groove >= candidate.timbre)
        return "Groove held steady, texture moved";
    if (candidate.melody >= candidate.timbre)
        return "Melody held steady, character shifted";
    return "Source character preserved";
}
}

bool RunModel::isValid() const noexcept
{
    return directory.isDirectory() && !candidates.empty();
}

const CandidateModel* RunModel::candidate(int rank) const noexcept
{
    const auto found = std::find_if(candidates.begin(), candidates.end(),
                                    [rank](const auto& item) { return item.rank == rank; });
    return found == candidates.end() ? nullptr : &*found;
}

CandidateModel* RunModel::candidate(int rank) noexcept
{
    const auto found = std::find_if(candidates.begin(), candidates.end(),
                                    [rank](const auto& item) { return item.rank == rank; });
    return found == candidates.end() ? nullptr : &*found;
}

RunModel RunModel::load(const juce::File& runDirectory)
{
    RunModel result;
    result.directory = runDirectory;
    const auto manifestValue = juce::JSON::parse(runDirectory.getChildFile("manifest.json"));
    if (const auto* manifest = manifestValue.getDynamicObject())
    {
        const auto config = manifest->getProperty("config");
        result.source = juce::File(config.getProperty("source", {}).toString());
        result.parentRunId = config.getProperty("parent_run_id", {}).toString();
        result.parentCandidate = static_cast<int>(config.getProperty("parent_candidate", 0));
        if (const auto* references = config.getProperty("references", {}).getArray())
            for (const auto& referenceValue : *references)
                if (const auto* reference = referenceValue.getArray(); reference != nullptr && !reference->isEmpty())
                    result.references.emplace_back((*reference)[0].toString());

        if (const auto* rows = manifest->getProperty("candidates").getArray())
            for (const auto& row : *rows)
                if (const auto* item = row.getDynamicObject())
                {
                    CandidateModel candidate;
                    candidate.rank = static_cast<int>(item->getProperty("rank"));
                    candidate.file = juce::File(item->getProperty("path").toString());
                    if (!candidate.file.existsAsFile())
                        candidate.file = runDirectory.getChildFile(
                            "cand_" + juce::String(candidate.rank).paddedLeft('0', 2) + ".wav");
                    candidate.referenceFit = static_cast<double>(item->getProperty("ref_fit"));
                    candidate.novelty = static_cast<double>(item->getProperty("novelty"));
                    candidate.taste = static_cast<double>(item->getProperty("taste"));
                    candidate.utility = static_cast<double>(item->getProperty("utility"));
                    const auto locks = item->getProperty("locks");
                    candidate.groove = static_cast<double>(locks.getProperty("groove", 0.0));
                    candidate.melody = static_cast<double>(locks.getProperty("melody", 0.0));
                    candidate.timbre = static_cast<double>(locks.getProperty("timbre", 0.0));
                    result.candidates.push_back(std::move(candidate));
                }
    }

    const auto candidateCount = static_cast<int>(result.candidates.size());
    for (auto& candidate : result.candidates)
        candidate.explanation = buildExplanation(candidate, candidateCount);

    const auto mapValue = juce::JSON::parse(runDirectory.getChildFile("map.json"));
    if (const auto* rows = mapValue.getArray())
        for (const auto& row : *rows)
            if (const auto* item = row.getDynamicObject())
                result.mapPoints.push_back({ item->getProperty("kind").toString(),
                                             juce::File(item->getProperty("path").toString()),
                                             static_cast<float>(item->getProperty("x")),
                                             static_cast<float>(item->getProperty("y")),
                                             static_cast<int>(item->getProperty("rank")) });
    return result;
}

void WorkflowModel::restoreFrom(const juce::ValueTree& state)
{
    audioSlots[0] = juce::File(state.getProperty("source", {}).toString());
    audioSlots[1] = juce::File(state.getProperty("reference1", {}).toString());
    audioSlots[2] = juce::File(state.getProperty("reference2", {}).toString());
    change = static_cast<int>(state.getProperty("change", 45));
    range = static_cast<int>(state.getProperty("range", 60));
    preserveGroove = static_cast<bool>(state.getProperty("preserveGroove", true));
    preserveMelody = static_cast<bool>(state.getProperty("preserveMelody", false));
    preserveTimbre = static_cast<bool>(state.getProperty("preserveTimbre", false));
    direction = state.getProperty("direction", {}).toString();
    activeRunId = state.getProperty("activeRunId", {}).toString();
    selectedCandidate = static_cast<int>(state.getProperty("selectedCandidate", 0));
    for (size_t index = 0; index < decisions.size(); ++index)
        decisions[index] = decisionFromString(
            state.getProperty("decision" + juce::String(static_cast<int>(index + 1)), {}).toString());
    view = audioSlots[0].existsAsFile() ? WorkflowViewState::ready : WorkflowViewState::needsSetup;
    if (activeRunId.isNotEmpty())
        view = WorkflowViewState::results;
}

void WorkflowModel::saveTo(juce::ValueTree& state) const
{
    state.setProperty("source", audioSlots[0].getFullPathName(), nullptr);
    state.setProperty("reference1", audioSlots[1].getFullPathName(), nullptr);
    state.setProperty("reference2", audioSlots[2].getFullPathName(), nullptr);
    state.setProperty("change", change, nullptr);
    state.setProperty("range", range, nullptr);
    state.setProperty("preserveGroove", preserveGroove, nullptr);
    state.setProperty("preserveMelody", preserveMelody, nullptr);
    state.setProperty("preserveTimbre", preserveTimbre, nullptr);
    state.setProperty("direction", direction, nullptr);
    state.setProperty("activeRunId", activeRunId, nullptr);
    state.setProperty("selectedCandidate", selectedCandidate, nullptr);
    for (size_t index = 0; index < decisions.size(); ++index)
        state.setProperty("decision" + juce::String(static_cast<int>(index + 1)),
                          decisionToString(decisions[index]), nullptr);
}

WorkflowModel WorkflowFixtures::make(WorkflowViewState state, const juce::File& fixtureRun)
{
    WorkflowModel fixture;
    fixture.view = state;
    fixture.audioSlots[0] = juce::File("/Fixtures/source.wav");
    fixture.audioSlots[1] = juce::File("/Fixtures/direction.wav");
    fixture.change = 52;
    fixture.preserveGroove = true;
    fixture.preserveMelody = true;
    if (state == WorkflowViewState::needsSetup)
        fixture.audioSlots = {};
    if (state == WorkflowViewState::results && fixtureRun.isDirectory())
    {
        fixture.activeRunId = fixtureRun.getFileName();
        fixture.selectedCandidate = 1;
    }
    return fixture;
}

juce::String decisionToString(CandidateDecision decision)
{
    switch (decision)
    {
        case CandidateDecision::keep: return "keep";
        case CandidateDecision::pass: return "pass";
        case CandidateDecision::favorite: return "favorite";
        case CandidateDecision::exported: return "exported";
        case CandidateDecision::none: break;
    }
    return {};
}

CandidateDecision decisionFromString(const juce::String& text)
{
    if (text == "keep") return CandidateDecision::keep;
    if (text == "pass") return CandidateDecision::pass;
    if (text == "favorite") return CandidateDecision::favorite;
    if (text == "exported") return CandidateDecision::exported;
    return CandidateDecision::none;
}
