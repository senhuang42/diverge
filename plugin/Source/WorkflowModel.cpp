#include "WorkflowModel.h"
#include <algorithm>

namespace
{
juce::String buildExplanation(const CandidateModel& candidate, int count)
{
    if (candidate.rank == count && count > 1)
        return "Wildcard - the widest departure";
    if (candidate.referenceFit >= 0.76)
        return "Closest to your direction";
    if (candidate.groove >= candidate.melody && candidate.groove >= candidate.timbre)
        return "Groove held steady, texture moved";
    if (candidate.melody >= candidate.timbre)
        return "Melody held steady, character shifted";
    return "Source character preserved";
}
}

bool CandidateChoices::value(CandidateDecision decision) const noexcept
{
    switch (decision)
    {
        case CandidateDecision::keep: return kept;
        case CandidateDecision::pass: return passed;
        case CandidateDecision::favorite: return favorite;
        case CandidateDecision::exported: return exported;
        case CandidateDecision::branched: return branched;
        case CandidateDecision::none: break;
    }
    return false;
}

void CandidateChoices::set(CandidateDecision decision, bool enabled) noexcept
{
    switch (decision)
    {
        case CandidateDecision::keep: kept = enabled; break;
        case CandidateDecision::pass: passed = enabled; break;
        case CandidateDecision::favorite: favorite = enabled; break;
        case CandidateDecision::exported: exported = enabled; break;
        case CandidateDecision::branched: branched = enabled; break;
        case CandidateDecision::none: break;
    }
}

CandidateDecision CandidateChoices::visualDecision() const noexcept
{
    if (favorite) return CandidateDecision::favorite;
    if (kept || branched) return CandidateDecision::keep;
    if (passed) return CandidateDecision::pass;
    if (exported) return CandidateDecision::exported;
    return CandidateDecision::none;
}

CandidateChoices CandidateChoices::fromLegacy(CandidateDecision decision) noexcept
{
    CandidateChoices result;
    result.set(decision, decision != CandidateDecision::none);
    return result;
}

bool RunModel::isValid() const noexcept
{
    return directory.isDirectory() && manifestLoaded;
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
        result.manifestLoaded = true;
        const auto config = manifest->getProperty("config");
        result.source = juce::File(config.getProperty("source", {}).toString());
        result.requestedCount = static_cast<int>(config.getProperty("n_return", 8));
        result.change = static_cast<int>(config.getProperty("transform", 45));
        result.range = static_cast<int>(config.getProperty("spread", 60));
        result.direction = config.getProperty("style_text_hint", {}).toString();
        const auto briefLocks = config.getProperty("locks", {});
        result.preserveGroove = false;
        if (const auto* lockNames = briefLocks.getArray())
            for (const auto& lock : *lockNames)
            {
                result.preserveGroove = result.preserveGroove || lock.toString() == "groove";
                result.preserveMelody = result.preserveMelody || lock.toString() == "melody";
                result.preserveTimbre = result.preserveTimbre || lock.toString() == "timbre";
            }
        result.parentRunId = config.getProperty("parent_run_id", {}).toString();
        result.parentCandidate = static_cast<int>(config.getProperty("parent_candidate", 0));
        const auto taste = manifest->getProperty("taste");
        result.tasteObservations = static_cast<int>(taste.getProperty("observations", 0));
        result.tasteConfidence = static_cast<double>(taste.getProperty("confidence", 0.0));
        result.opinion = static_cast<int>(taste.getProperty("opinion", 50));
        result.tasteWarning = taste.getProperty("warning", {}).toString();
        const auto selection = manifest->getProperty("selection");
        result.requestedCount = static_cast<int>(
            selection.getProperty("requested_count", result.requestedCount));
        result.returnedCount = static_cast<int>(selection.getProperty("returned_count", 0));
        result.shortfall = static_cast<int>(selection.getProperty("shortfall", 0));
        result.canTryMore = static_cast<bool>(selection.getProperty("can_try_more", false));
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
                    const auto tasteUncertainty = item->getProperty("taste_uncertainty");
                    candidate.tasteUncertainty = tasteUncertainty.isVoid()
                                                     ? 1.0
                                                     : static_cast<double>(tasteUncertainty);
                    const auto tasteEvidence = item->getProperty("taste_evidence");
                    candidate.tasteEvidence = tasteEvidence.isVoid()
                                                  ? 0.0
                                                  : static_cast<double>(tasteEvidence);
                    candidate.tasteMode = item->getProperty("taste_mode").toString();
                    candidate.role = item->getProperty("role").toString();
                    candidate.utility = static_cast<double>(item->getProperty("utility"));
                    const auto locks = item->getProperty("locks");
                    candidate.groove = static_cast<double>(locks.getProperty("groove", 0.0));
                    candidate.melody = static_cast<double>(locks.getProperty("melody", 0.0));
                    candidate.timbre = static_cast<double>(locks.getProperty("timbre", 0.0));
                    result.candidates.push_back(std::move(candidate));
                }
    }

    const auto candidateCount = static_cast<int>(result.candidates.size());
    if (result.returnedCount <= 0) result.returnedCount = candidateCount;
    if (result.shortfall <= 0)
        result.shortfall = juce::jmax(0, result.requestedCount - result.returnedCount);
    result.canTryMore = result.canTryMore || result.shortfall > 0;
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
    opinion = static_cast<int>(state.getProperty("opinion", 50));
    learningEnabled = static_cast<bool>(state.getProperty("learningEnabled", true));
    preserveGroove = static_cast<bool>(state.getProperty("preserveGroove", true));
    preserveMelody = static_cast<bool>(state.getProperty("preserveMelody", false));
    preserveTimbre = static_cast<bool>(state.getProperty("preserveTimbre", false));
    direction = state.getProperty("direction", {}).toString();
    activeRunId = state.getProperty("activeRunId", {}).toString();
    selectedCandidate = static_cast<int>(state.getProperty("selectedCandidate", 0));
    for (size_t index = 0; index < decisions.size(); ++index)
    {
        const auto suffix = juce::String(static_cast<int>(index + 1));
        const auto hasIndependentState = state.hasProperty("kept" + suffix)
                                         || state.hasProperty("favorite" + suffix)
                                         || state.hasProperty("passed" + suffix)
                                         || state.hasProperty("exported" + suffix)
                                         || state.hasProperty("branched" + suffix);
        if (hasIndependentState)
        {
            choices[index].kept = static_cast<bool>(state.getProperty("kept" + suffix, false));
            choices[index].passed = static_cast<bool>(state.getProperty("passed" + suffix, false));
            choices[index].favorite = static_cast<bool>(state.getProperty("favorite" + suffix, false));
            choices[index].exported = static_cast<bool>(state.getProperty("exported" + suffix, false));
            choices[index].branched = static_cast<bool>(state.getProperty("branched" + suffix, false));
        }
        else
            choices[index] = CandidateChoices::fromLegacy(decisionFromString(
                state.getProperty("decision" + suffix, {}).toString()));
    }
    refreshVisualDecisions();
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
    state.setProperty("opinion", opinion, nullptr);
    state.setProperty("learningEnabled", learningEnabled, nullptr);
    state.setProperty("preserveGroove", preserveGroove, nullptr);
    state.setProperty("preserveMelody", preserveMelody, nullptr);
    state.setProperty("preserveTimbre", preserveTimbre, nullptr);
    state.setProperty("direction", direction, nullptr);
    state.setProperty("activeRunId", activeRunId, nullptr);
    state.setProperty("selectedCandidate", selectedCandidate, nullptr);
    for (size_t index = 0; index < decisions.size(); ++index)
    {
        const auto suffix = juce::String(static_cast<int>(index + 1));
        state.setProperty("decision" + juce::String(static_cast<int>(index + 1)),
                          decisionToString(decisions[index]), nullptr);
        state.setProperty("kept" + suffix, choices[index].kept, nullptr);
        state.setProperty("passed" + suffix, choices[index].passed, nullptr);
        state.setProperty("favorite" + suffix, choices[index].favorite, nullptr);
        state.setProperty("exported" + suffix, choices[index].exported, nullptr);
        state.setProperty("branched" + suffix, choices[index].branched, nullptr);
    }
}

void WorkflowModel::refreshVisualDecisions() noexcept
{
    for (size_t index = 0; index < decisions.size(); ++index)
        decisions[index] = choices[index].visualDecision();
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
        case CandidateDecision::branched: return "branched";
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
    if (text == "branched") return CandidateDecision::branched;
    return CandidateDecision::none;
}
