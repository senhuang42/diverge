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
    exported,
    branched
};

struct CandidateChoices
{
    bool kept = false;
    bool passed = false;
    bool favorite = false;
    bool exported = false;
    bool branched = false;

    bool value(CandidateDecision decision) const noexcept;
    void set(CandidateDecision decision, bool enabled) noexcept;
    bool positive() const noexcept { return kept || favorite || exported || branched; }
    CandidateDecision visualDecision() const noexcept;
    static CandidateChoices fromLegacy(CandidateDecision decision) noexcept;
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
    double novelty = 0.0;
    double taste = 0.0;
    double tasteUncertainty = 1.0;
    double tasteEvidence = 0.0;
    juce::String tasteMode;
    juce::String role;
    double utility = 0.0;
};

struct RunModel
{
    juce::File directory;
    bool manifestLoaded = false;
    juce::File source;
    std::vector<juce::File> references;
    std::vector<CandidateModel> candidates;
    std::vector<MapPoint> mapPoints;
    int change = 45;
    int referenceMix = 50;
    int range = 60;
    juce::String direction;
    juce::String parentRunId;
    int parentCandidate = 0;
    int tasteObservations = 0;
    double tasteConfidence = 0.0;
    int opinion = 50;
    int requestedCount = 8;
    int returnedCount = 0;
    int shortfall = 0;
    bool canTryMore = false;
    juce::String tasteWarning;

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
    int referenceMix = 50;
    int range = 60;
    int opinion = 50;
    bool learningEnabled = true;
    juce::String direction;
    juce::String activeRunId;
    int selectedCandidate = 0;
    std::array<CandidateChoices, 8> choices {};
    std::array<CandidateDecision, 8> decisions {};

    void refreshVisualDecisions() noexcept;

    void restoreFrom(const juce::ValueTree& state);
    void saveTo(juce::ValueTree& state) const;
};

struct WorkflowFixtures
{
    static WorkflowModel make(WorkflowViewState state, const juce::File& fixtureRun = {});
};

juce::String decisionToString(CandidateDecision decision);
CandidateDecision decisionFromString(const juce::String& text);
