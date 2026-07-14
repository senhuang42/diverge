#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <vector>

enum class WorkflowViewState
{
    needsSetup,
    ready,
    generating,
    loading,
    results,
    cancelled,
    recoverableError,
    fatalSetupError
};

enum class CandidateDecision
{
    none,
    keep,
    pass,
    favorite,
    exported
};

struct MapPoint
{
    juce::String kind;
    juce::File path;
    float x = 0.0f;
    float y = 0.0f;
    int rank = 0;
};

struct CandidateModel
{
    int rank = 0;
    juce::File file;
    juce::String explanation;
    CandidateDecision decision = CandidateDecision::none;
    double referenceFit = 0.0;
    double groove = 0.0;
    double melody = 0.0;
    double timbre = 0.0;
    double novelty = 0.0;
    double taste = 0.0;
    double utility = 0.0;
};

struct RunModel
{
    juce::File directory;
    juce::File source;
    std::vector<juce::File> references;
    std::vector<CandidateModel> candidates;
    std::vector<MapPoint> mapPoints;
    juce::String parentRunId;
    int parentCandidate = 0;

    bool isValid() const noexcept;
    const CandidateModel* candidate(int rank) const noexcept;
    CandidateModel* candidate(int rank) noexcept;
    static RunModel load(const juce::File& directory);
};

struct WorkflowModel
{
    WorkflowViewState view = WorkflowViewState::needsSetup;
    std::array<juce::File, 3> audioSlots;
    int change = 45;
    int range = 60;
    bool preserveGroove = true;
    bool preserveMelody = false;
    bool preserveTimbre = false;
    juce::String direction;
    juce::String activeRunId;
    int selectedCandidate = 0;
    std::array<CandidateDecision, 8> decisions {};

    void restoreFrom(const juce::ValueTree& state);
    void saveTo(juce::ValueTree& state) const;
};

struct WorkflowFixtures
{
    static WorkflowModel make(WorkflowViewState state, const juce::File& fixtureRun = {});
};

juce::String decisionToString(CandidateDecision decision);
CandidateDecision decisionFromString(const juce::String& text);
