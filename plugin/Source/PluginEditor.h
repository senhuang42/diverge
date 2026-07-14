#pragma once

#include "JobRunner.h"
#include "PluginProcessor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <deque>

struct MapPoint
{
    juce::String kind;
    juce::File path;
    float x = 0.0f;
    float y = 0.0f;
    int rank = 0;
};

class MapComponent final : public juce::Component
{
public:
    std::function<void(int)> onCandidateSelected;
    void setPoints(std::vector<MapPoint> next);
    void setSelectedRank(int rank);
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    juce::Point<float> positionFor(const MapPoint&) const;
    std::vector<MapPoint> points;
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
    bool isInterestedInFileDrag(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray&, int, int) override;

private:
    void timerCallback() override;
    void chooseAudio(int slot);
    void setAudioSlot(int slot, const juce::File&);
    void toggleCapture();
    void startGeneration();
    juce::File writeRunConfig() const;
    void loadRun(const juce::File&);
    void selectCandidate(int rank);
    void recordDecision(const juce::String& label);
    void runCriticCommand(const juce::StringArray& arguments);
    juce::File choicesFile() const;
    juce::File tasteEventsFile() const;
    juce::File tasteModelFile() const;
    void trainCritic();
    void pollCriticProcess();
    void restoreSettings();
    void saveSettings();
    juce::String styleHint() const;

    DivergeAudioProcessor& audioProcessor;
    JobRunner job;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::ChildProcess> decisionProcess;
    juce::String criticAction;
    std::deque<juce::StringArray> criticQueue;
    int choicesSinceTraining = 0;
    int totalChoiceCount = 0;
    juce::String criticCandidatePath;
    std::array<juce::String, 8> lastTasteEventIds;

    juce::Label titleLabel;
    juce::Label tasteLabel;
    juce::TextButton sourceButton { "Drop or choose source" };
    juce::TextButton recordButton { "Record input" };
    std::array<juce::TextButton, 2> referenceButtons {
        juce::TextButton { "Choose reference 1" }, juce::TextButton { "Choose reference 2" }
    };
    std::array<juce::Slider, 2> referenceWeights;
    juce::ToggleButton grooveLock { "Groove" };
    juce::ToggleButton melodyLock { "Melody" };
    juce::ToggleButton timbreLock { "Timbre" };
    juce::Slider transformSlider;
    juce::Slider spreadSlider;
    juce::Slider driftSlider;
    juce::Slider opinionSlider;
    juce::Label transformLabel;
    juce::Label spreadLabel;
    juce::Label driftLabel;
    juce::Label opinionLabel;
    juce::ToggleButton fastMode { "Fast mode" };
    juce::TextEditor styleEditor;
    juce::TextButton generateButton { "Generate" };
    juce::TextButton cancelButton { "Cancel" };
    juce::Label progressLabel;

    MapComponent map;
    std::array<juce::TextButton, 8> candidateButtons;
    juce::TextButton auditionButton { "Audition" };
    juce::TextButton loveButton { "Love" };
    juce::TextButton keepButton { "Keep" };
    juce::TextButton discardButton { "Discard" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton dragButton { "Drag WAV to DAW" };
    juce::Label candidateDetail;

    juce::TextButton settingsButton { "Settings" };
    juce::Component settingsPanel;
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

    std::array<juce::File, 3> audioSlots;
    std::vector<MapPoint> mapPoints;
    std::array<juce::File, 8> candidates;
    std::array<juce::String, 8> candidateDescriptions;
    juce::File currentRun;
    int selectedCandidate = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DivergeAudioProcessorEditor)
};
