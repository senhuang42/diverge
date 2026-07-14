#pragma once

#include "DesignSystem.h"
#include "PluginProcessor.h"
#include <deque>

class MapComponent final : public juce::Component
{
public:
    std::function<void(int)> onCandidateSelected;
    void setPoints(std::vector<MapPoint> next);
    void setSelectedRank(int rank);
    void setDecisions(const std::array<CandidateDecision, 8>& next);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Point<float> positionFor(const MapPoint&) const;
    std::vector<MapPoint> points;
    std::array<CandidateDecision, 8> decisions {};
    int selectedRank = 0;
};

class DivergeAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           public juce::FileDragAndDropTarget,
                                           private juce::Timer
{
public:
    explicit DivergeAudioProcessorEditor(DivergeAudioProcessor&);
    ~DivergeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void configureUi();
    bool applyUiFixture();
    void setPrepareVisible(bool visible);
    void setSettingsVisible(bool visible);
    void setAdvancedVisible(bool visible);
    void chooseAudio(int slot);
    void setAudioSlot(int slot, const juce::File&);
    void toggleCapture();
    void togglePreview(const juce::File&, int candidateRank = 0, bool source = false);
    void updateTransportUi();
    void startGeneration();
    juce::File writeRunConfig() const;
    void loadRun(const juce::File&);
    void selectCandidate(int rank, bool playImmediately = true);
    void recordDecision(CandidateDecision);
    void undoDecision();
    void dragSelected();
    void branchFromSelected();
    void createNew();
    void adjustRange(int delta);
    void showToast(const juce::String&);

    void runCriticCommand(const juce::StringArray& arguments);
    void trainCritic();
    void pollCriticProcess();
    juce::File choicesFile() const;
    juce::File tasteEventsFile() const;
    juce::File tasteModelFile() const;
    void restoreSettings();
    void saveSettings();
    juce::String styleHint() const;

    DivergeAudioProcessor& audioProcessor;
    DivergeLookAndFeel lookAndFeel;
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache { 64 };
    std::unique_ptr<juce::FileChooser> chooser;

    WorkflowModel workflow;
    RunModel loadedRun;
    std::array<juce::File, 3> audioSlots;
    juce::File currentRun;
    int selectedCandidate = 0;
    int playingCandidate = 0;
    bool playingSource = false;
    bool showPrepare = true;
    bool showMap = false;
    bool showDirectionText = false;
    bool showAdvanced = false;
    bool dragHover = false;
    juce::String fixtureMode;
    int toastTicks = 0;
    float displayedProgress = 0.0f;
    JobRunner::Status lastJobStatus = JobRunner::Status::idle;

    juce::Label brandLabel;
    juce::Label promiseLabel;
    juce::Label localBadge;
    juce::TextButton settingsButton { "Settings" };

    juce::Label sourceSection;
    juce::Label directionSection;
    juce::Label changeSection;
    juce::Label preserveSection;
    std::unique_ptr<WaveformCard> sourceCard;
    std::unique_ptr<WaveformCard> directionCard;
    juce::TextButton recordButton { "Record" };
    juce::TextButton addDirectionButton { "+ Add direction" };
    juce::TextEditor styleEditor;
    juce::Slider changeSlider;
    juce::Label familiarLabel;
    juce::Label wildLabel;
    juce::ToggleButton grooveLock { "Groove" };
    juce::ToggleButton melodyLock { "Melody" };
    juce::ToggleButton timbreLock { "Timbre" };
    juce::TextButton generateButton { "Create 8 variations" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label progressLabel;
    juce::Label privacyLabel;

    juce::TextButton briefButton { "<  Brief" };
    juce::Label resultsTitle;
    juce::TextButton gridButton { "Grid" };
    juce::TextButton mapButton { "Map" };
    juce::TextButton newButton { "Create new" };
    std::array<std::unique_ptr<WaveformCard>, 8> candidateCards;
    MapComponent map;
    juce::Label selectedTitle;
    juce::Label candidateDetail;
    juce::TextButton abButton { "A/B Source" };
    juce::TextButton passButton { "Pass" };
    juce::TextButton keepButton { "Keep" };
    juce::TextButton favoriteButton { "Favorite" };
    juce::TextButton branchButton { "More like this" };
    juce::TextButton dragButton { "Use in DAW  ->" };
    juce::TextButton tighterButton { "Tighter next" };
    juce::TextButton widerButton { "Wider next" };
    juce::Label shortcutLabel;
    juce::Label toastLabel;

    juce::Component settingsPanel;
    juce::Label settingsTitle;
    juce::TextButton settingsClose { "Done" };
    juce::Label studioStatus;
    juce::Label learningStatus;
    juce::Label libraryStatus;
    juce::TextButton advancedButton { "Advanced diagnostics" };
    juce::Label pythonLabel;
    juce::TextEditor pythonEditor;
    juce::Label modelsLabel;
    juce::TextEditor modelsEditor;
    juce::Label libraryLabel;
    juce::TextEditor libraryEditor;
    juce::Label choicesLabel;
    juce::TextEditor choicesEditor;
    juce::Label outputLabel;
    juce::TextEditor outputEditor;

    std::unique_ptr<juce::ChildProcess> decisionProcess;
    juce::String criticAction;
    juce::String criticCandidatePath;
    std::deque<juce::StringArray> criticQueue;
    std::array<juce::String, 8> lastTasteEventIds;
    int totalChoiceCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DivergeAudioProcessorEditor)
};
