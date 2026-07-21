#include "PluginEditor.h"
#include <algorithm>
#include <cmath>

namespace
{
void configureSectionLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(DivergeTheme::label(10.5f));
    label.setColour(juce::Label::textColourId, DivergeTheme::muted);
}

void configureSupportingLabel(juce::Label& label, const juce::String& text,
                              juce::Justification justification = juce::Justification::centredLeft)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(DivergeTheme::body(12.5f));
    label.setColour(juce::Label::textColourId, DivergeTheme::muted);
    label.setJustificationType(justification);
}

juce::String quotedPath(const juce::File& file) { return file.getFullPathName(); }

juce::String friendlyError(const juce::String& error)
{
    if (error.containsIgnoreCase("model") || error.containsIgnoreCase("checkpoint"))
        return "Models need attention - open Settings to locate the local model folder";
    if (error.containsIgnoreCase("space") || error.containsIgnoreCase("disk"))
        return "Not enough free storage - choose another Storage location in Settings";
    if (error.containsIgnoreCase("python") || error.containsIgnoreCase("process"))
        return "Local engine unavailable - open Settings for a quick health check";
    if (error.containsIgnoreCase("audio") || error.containsIgnoreCase("decode"))
        return "That audio could not be read - try WAV, AIFF, FLAC, MP3, or M4A";
    return error.isNotEmpty() ? "Creation stopped - " + error : "Creation stopped before a run was saved";
}
}

void MapComponent::setPoints(std::vector<MapPoint> next)
{
    points = std::move(next);
    repaint();
}

void MapComponent::setSelectedRank(int rank)
{
    selectedRank = rank;
    repaint();
}

void MapComponent::setDecisions(const std::array<CandidateDecision, 8>& next)
{
    if (decisions == next) return;
    decisions = next;
    repaint();
}

juce::Point<float> MapComponent::positionFor(const MapPoint& point) const
{
    if (points.empty()) return getLocalBounds().toFloat().getCentre();
    auto minX = points.front().x, maxX = points.front().x;
    auto minY = points.front().y, maxY = points.front().y;
    for (const auto& item : points)
    {
        minX = juce::jmin(minX, item.x); maxX = juce::jmax(maxX, item.x);
        minY = juce::jmin(minY, item.y); maxY = juce::jmax(maxY, item.y);
    }
    const auto bounds = getLocalBounds().toFloat().reduced(42.0f);
    const auto nx = (point.x - minX) / juce::jmax(0.0001f, maxX - minX);
    const auto ny = (point.y - minY) / juce::jmax(0.0001f, maxY - minY);
    return { bounds.getX() + nx * bounds.getWidth(), bounds.getBottom() - ny * bounds.getHeight() };
}

void MapComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setGradientFill({ DivergeTheme::surface.brighter(0.02f), bounds.getX(), bounds.getY(),
                        DivergeTheme::surface.darker(0.12f), bounds.getX(), bounds.getBottom(), false });
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
    g.setColour(DivergeTheme::hairline);
    g.drawRoundedRectangle(bounds, DivergeTheme::radius, 1.0f);
    if (points.empty())
    {
        g.setColour(DivergeTheme::muted);
        g.setFont(DivergeTheme::body(14.5f));
        g.drawFittedText("The variation map appears after a batch is ready.", getLocalBounds().reduced(48),
                         juce::Justification::centred, 2);
        return;
    }

    juce::Point<float> sourcePoint;
    for (const auto& point : points)
        if (point.kind == "source") sourcePoint = positionFor(point);

    juce::Graphics::ScopedSaveState clip(g);
    juce::Path panel;
    panel.addRoundedRectangle(bounds.reduced(1.0f), DivergeTheme::radius - 1.0f);
    g.reduceClipRegion(panel);

    // Distance rings around the source: near stays familiar, far diverges.
    for (int ring = 1; ring <= 3; ++ring)
    {
        const auto ringRadius = static_cast<float>(ring) * bounds.getHeight() * 0.28f;
        g.setColour(DivergeTheme::hairline.withAlpha(0.75f - static_cast<float>(ring) * 0.15f));
        g.drawEllipse(juce::Rectangle<float>(ringRadius * 2.0f, ringRadius * 2.0f)
                          .withCentre(sourcePoint), 1.0f);
    }

    for (const auto& point : points)
        if (point.rank > 0)
        {
            const auto target = positionFor(point);
            const auto middle = sourcePoint + (target - sourcePoint) * 0.5f;
            const auto delta = target - sourcePoint;
            const juce::Point<float> bow { -delta.y * 0.12f, delta.x * 0.12f };
            juce::Path trace;
            trace.startNewSubPath(sourcePoint);
            trace.quadraticTo(middle + bow, target);
            const auto isSelected = point.rank == selectedRank;
            g.setColour(isSelected ? DivergeTheme::exploration.withAlpha(0.55f)
                                   : DivergeTheme::edge.withAlpha(0.55f));
            g.strokePath(trace, juce::PathStrokeType(isSelected ? 1.6f : 1.0f));
        }

    for (const auto& point : points)
    {
        const auto position = positionFor(point);
        if (point.kind == "source")
        {
            g.setColour(DivergeTheme::exploration.withAlpha(0.30f));
            g.drawEllipse(juce::Rectangle<float>(24.0f, 24.0f).withCentre(position), 1.2f);
            g.setColour(DivergeTheme::text);
            g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre(position));
            g.setColour(DivergeTheme::dim);
            g.setFont(DivergeTheme::label(8.5f));
            g.drawText("SOURCE", juce::Rectangle<float>(64.0f, 14.0f)
                           .withCentre(position.translated(0.0f, 23.0f)),
                       juce::Justification::centred, false);
        }
        else if (point.kind == "reference")
        {
            juce::Path diamond;
            diamond.addQuadrilateral(position.x, position.y - 8.0f, position.x + 8.0f, position.y,
                                     position.x, position.y + 8.0f, position.x - 8.0f, position.y);
            g.setColour(DivergeTheme::decisionSoft);
            g.fillPath(diamond);
            g.setColour(DivergeTheme::decision);
            g.strokePath(diamond, juce::PathStrokeType(1.3f));
        }
        else
        {
            const auto isSelected = point.rank == selectedRank;
            auto colour = isSelected ? DivergeTheme::exploration : DivergeTheme::muted;
            if (point.rank >= 1 && point.rank <= 8)
            {
                const auto decision = decisions[static_cast<size_t>(point.rank - 1)];
                if (decision == CandidateDecision::keep || decision == CandidateDecision::favorite
                    || decision == CandidateDecision::exported)
                    colour = isSelected ? DivergeTheme::exploration : DivergeTheme::decision;
                else if (decision == CandidateDecision::pass)
                    colour = isSelected ? DivergeTheme::exploration : DivergeTheme::dim;
            }
            const auto size = isSelected ? 32.0f : 25.0f;
            const auto node = juce::Rectangle<float>(size, size).withCentre(position);
            if (isSelected)
                for (int halo = 1; halo <= 3; ++halo)
                {
                    g.setColour(DivergeTheme::exploration.withAlpha(0.14f / static_cast<float>(halo)));
                    g.drawEllipse(node.expanded(static_cast<float>(halo) * 3.0f), 2.0f);
                }
            g.setColour(isSelected ? DivergeTheme::exploration : DivergeTheme::raised.brighter(0.05f));
            g.fillEllipse(node);
            g.setColour(isSelected ? DivergeTheme::exploration : colour.withAlpha(0.9f));
            g.drawEllipse(node, isSelected ? 1.6f : 1.3f);
            g.setColour(isSelected ? DivergeTheme::canvas : colour);
            g.setFont(DivergeTheme::monoBold(11.0f));
            g.drawText(juce::String(point.rank), node, juce::Justification::centred);
        }
    }
}

void MapComponent::mouseDown(const juce::MouseEvent& event)
{
    float bestDistance = 30.0f;
    int bestRank = 0;
    for (const auto& point : points)
        if (point.rank > 0)
        {
            const auto distance = positionFor(point).getDistanceFrom(event.position);
            if (distance < bestDistance) { bestDistance = distance; bestRank = point.rank; }
        }
    if (bestRank > 0 && onCandidateSelected) onCandidateSelected(bestRank);
}

DivergeAudioProcessorEditor::DivergeAudioProcessorEditor(DivergeAudioProcessor& owner)
    : AudioProcessorEditor(&owner), audioProcessor(owner)
{
    formatManager.registerBasicFormats();
    sourceCard = std::make_unique<WaveformCard>(formatManager, thumbnailCache);
    directionCard = std::make_unique<WaveformCard>(formatManager, thumbnailCache);
    for (auto& card : candidateCards)
        card = std::make_unique<WaveformCard>(formatManager, thumbnailCache);
    for (auto& card : recentCards)
        card = std::make_unique<WaveformCard>(formatManager, thumbnailCache);

    configureUi();
    const auto fixtureSize = juce::SystemStats::getEnvironmentVariable("DIVERGE_UI_SIZE", {}).toLowerCase();
    if (fixtureSize == "minimum") setSize(900, 680);
    else if (fixtureSize == "large") setSize(1440, 960);
    restoreSettings();
    if (!applyUiFixture())
    {
        setPrepareVisible(workflow.activeRunId.isEmpty());
        if (workflow.activeRunId.isNotEmpty())
        {
            const auto restoredRun = juce::File(outputEditor.getText().trim()).getChildFile(workflow.activeRunId);
            if (restoredRun.isDirectory()) loadRun(restoredRun);
            else
            {
                setPrepareVisible(true);
                showToast("A previous run moved - repair its Storage location in Settings");
            }
        }
    }
    const auto snapshotPath = juce::SystemStats::getEnvironmentVariable("DIVERGE_UI_SNAPSHOT", {});
    if (snapshotPath.isNotEmpty())
    {
        snapshotFile = juce::File(snapshotPath);
        snapshotTicks = 70;
    }
    trainCritic();
    startTimerHz(60);
}

bool DivergeAudioProcessorEditor::applyUiFixture()
{
    fixtureMode = juce::SystemStats::getEnvironmentVariable("DIVERGE_UI_FIXTURE", {}).toLowerCase();
    if (fixtureMode.isEmpty()) return false;
   #if defined(DIVERGE_PROJECT_ROOT)
    const auto project = juce::File(juce::String(DIVERGE_PROJECT_ROOT));
   #else
    const auto project = juce::File::getCurrentWorkingDirectory();
   #endif
    if (fixtureMode == "empty")
    {
        audioSlots = {};
        refreshSlotCard(0);
        refreshSlotCard(1);
        setPrepareVisible(true);
        return true;
    }
    const auto fixture = WorkflowFixtures::make(
        (fixtureMode == "results" || fixtureMode == "recent" || fixtureMode == "map")
            ? WorkflowViewState::results
        : fixtureMode == "generating" ? WorkflowViewState::generating
        : fixtureMode == "error" ? WorkflowViewState::recoverableError
        : WorkflowViewState::ready);
    workflow.change = fixture.change;
    changeSlider.setValue(workflow.change);
    audioSlots[0] = project.getChildFile("data/loop_a.wav");
    audioSlots[1] = project.getChildFile("data/ref_a.wav");
    refreshSlotCard(0);
    refreshSlotCard(1);
    setPrepareVisible(true);
    if (fixtureMode == "generating")
    {
        generateButton.setEnabled(false);
        generateButton.setButtonText("Creating...");
        progressLabel.setText("Creating candidates  /  7/16", juce::dontSendNotification);
        displayedProgress = 7.0f / 16.0f;
    }
    else if (fixtureMode == "error")
    {
        progressLabel.setText("Local engine unavailable - open Settings for a quick health check",
                              juce::dontSendNotification);
        progressLabel.setColour(juce::Label::textColourId, DivergeTheme::danger);
    }
    else if (fixtureMode == "settings")
    {
        setSettingsVisible(true);
        setAdvancedVisible(true);
    }
    else if (fixtureMode == "results" || fixtureMode == "recent" || fixtureMode == "map")
    {
        auto run = juce::File(juce::SystemStats::getEnvironmentVariable("DIVERGE_FIXTURE_RUN", {}));
        if (!run.isDirectory())
        {
            juce::Array<juce::File> runs;
            project.getChildFile("runs").findChildFiles(runs, juce::File::findDirectories, false);
            std::sort(runs.begin(), runs.end(), [](const auto& a, const auto& b)
            {
                return a.getLastModificationTime() > b.getLastModificationTime();
            });
            for (const auto& candidate : runs)
                if (candidate.getChildFile("manifest.json").existsAsFile()) { run = candidate; break; }
        }
        if (run.isDirectory()) loadRun(run);
        if (fixtureMode == "recent") setRecentVisible(true);
        if (fixtureMode == "map") mapButton.triggerClick();
    }
    return true;
}

DivergeAudioProcessorEditor::~DivergeAudioProcessorEditor()
{
    saveSettings();
    audioProcessor.stopPreview();
    criticQueue.clear();
    if (decisionProcess)
    {
        if (decisionProcess->isRunning()) decisionProcess->kill();
        decisionProcess->waitForProcessToFinish(1000);
        decisionProcess.reset();
    }
    setLookAndFeel(nullptr);
}

void DivergeAudioProcessorEditor::configureUi()
{
    setLookAndFeel(&lookAndFeel);
    setSize(1120, 760);
    setResizable(true, true);
    setResizeLimits(900, 680, 1600, 1100);
    setWantsKeyboardFocus(true);

    for (auto* component : std::initializer_list<juce::Component*> {
             &brandLabel, &promiseLabel, &localBadge, &settingsButton,
             &sourceSection, sourceCard.get(), &recordButton, &directionSection, directionCard.get(),
             &replaceDirectionButton, &removeDirectionButton, &addDirectionButton, &styleEditor,
             &changeSection, &changeSlider, &familiarLabel, &wildLabel,
             &preserveSection, &grooveLock, &melodyLock, &timbreLock, &generateButton, &cancelButton,
             &progressLabel, &privacyLabel, &briefButton, &resultsTitle, &gridButton, &mapButton, &newButton,
             &map, &selectedTitle, &candidateDetail, &abButton, &passButton, &keepButton, &favoriteButton,
             &branchButton, &dragButton, &tighterButton, &widerButton, &shortcutLabel,
             &comparisonLabel, &comparisonAButton, &comparisonBButton,
             &comparisonNeitherButton, &comparisonSkipButton,
             &keptButton, &recentButton, &scrim, &settingsPanel, &recentPanel, &toast })
        addAndMakeVisible(component);
    for (auto& card : candidateCards) addAndMakeVisible(card.get());
    scrim.setVisible(false);
    scrim.setAlpha(0.0f);
    scrim.onDismiss = [this] { setRecentVisible(false); };
    toast.setVisible(false);

    brandLabel.setText("DIVERGE", juce::dontSendNotification);
    brandLabel.setFont(DivergeTheme::display(20.0f));
    promiseLabel.setText("Recognizable where you choose. Different where it matters.", juce::dontSendNotification);
    promiseLabel.setFont(DivergeTheme::body(12.5f));
    promiseLabel.setColour(juce::Label::textColourId, DivergeTheme::muted);
    localBadge.setText("LOCAL", juce::dontSendNotification);
    localBadge.setFont(DivergeTheme::monoBold(10.0f));
    localBadge.setJustificationType(juce::Justification::centred);
    localBadge.setColour(juce::Label::textColourId, DivergeTheme::exploration);
    settingsButton.setButtonText("...");

    configureSectionLabel(sourceSection, "SOURCE");
    configureSectionLabel(directionSection, "DIRECTION  /  OPTIONAL");
    configureSectionLabel(changeSection, "CHANGE");
    configureSectionLabel(preserveSection, "PRESERVE");
    refreshSlotCard(0);
    refreshSlotCard(1);
    sourceCard->onChoose = [this] { chooseAudio(0); };
    sourceCard->onActivate = [this] { togglePreview(audioSlots[0], 0, true); };
    sourceCard->onSeek = [this](double position) { seekPreview(audioSlots[0], position, 0, true); };
    directionCard->onChoose = [this] { chooseAudio(1); };
    directionCard->onActivate = [this] { togglePreview(audioSlots[1]); };
    directionCard->onSeek = [this](double position) { seekPreview(audioSlots[1], position); };
    replaceDirectionButton.onClick = [this] { chooseAudio(1); };
    replaceDirectionButton.setTooltip("Choose a different direction reference");
    removeDirectionButton.onClick = [this]
    {
        const auto removedPath = audioSlots[1].getFullPathName();
        if (audioProcessor.previewPath() == removedPath) audioProcessor.stopPreview();
        setAudioSlot(1, juce::File());
        showToast("Direction reference removed");
    };
    removeDirectionButton.setTooltip("Remove the direction reference from this brief");
    removeDirectionButton.setColour(juce::TextButton::textColourOffId, DivergeTheme::danger);
    recordButton.onClick = [this] { toggleCapture(); };
    addDirectionButton.onClick = [this]
    {
        showDirectionText = !showDirectionText;
        styleEditor.setVisible(showDirectionText && showPrepare);
        addDirectionButton.setButtonText(showDirectionText ? "- Hide text" : "+ Text direction");
        resized();
    };
    styleEditor.setTextToShowWhenEmpty("Describe a texture, energy, or production direction...", DivergeTheme::muted);
    styleEditor.setMultiLine(false);

    changeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    changeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    changeSlider.setRange(0.0, 100.0, 1.0);
    changeSlider.setValue(45.0);
    changeSlider.setTooltip("How far each result may move from the source");
    configureSupportingLabel(familiarLabel, "FAMILIAR");
    familiarLabel.setFont(DivergeTheme::label(9.5f));
    familiarLabel.setColour(juce::Label::textColourId, DivergeTheme::dim);
    configureSupportingLabel(wildLabel, "UNRECOGNIZABLE", juce::Justification::centredRight);
    wildLabel.setFont(DivergeTheme::label(9.5f));
    wildLabel.setColour(juce::Label::textColourId, DivergeTheme::dim);
    grooveLock.setToggleState(true, juce::dontSendNotification);
    for (auto* lock : { &grooveLock, &melodyLock, &timbreLock }) lock->setClickingTogglesState(true);

    generateButton.setColour(juce::TextButton::buttonColourId, DivergeTheme::exploration);
    generateButton.setColour(juce::TextButton::textColourOffId, DivergeTheme::canvas);
    generateButton.onClick = [this] { startGeneration(); };
    cancelButton.onClick = [this] { audioProcessor.generation().cancel(); };
    cancelButton.setVisible(false);
    configureSupportingLabel(progressLabel, "Ready when you are", juce::Justification::centred);
    configureSupportingLabel(privacyLabel, "Audio stays on this Mac.", juce::Justification::centred);
    privacyLabel.setFont(DivergeTheme::body(11.5f));
    privacyLabel.setColour(juce::Label::textColourId, DivergeTheme::dim);

    briefButton.onClick = [this] { setPrepareVisible(true); };
    resultsTitle.setText("8 VARIATIONS", juce::dontSendNotification);
    resultsTitle.setFont(DivergeTheme::display(15.0f));
    gridButton.setClickingTogglesState(true);
    mapButton.setClickingTogglesState(true);
    gridButton.setToggleState(true, juce::dontSendNotification);
    gridButton.onClick = [this]
    {
        showMap = false;
        gridButton.setToggleState(true, juce::dontSendNotification);
        mapButton.setToggleState(false, juce::dontSendNotification);
        map.setVisible(false);
        updateResultVisibility();
        resized();
    };
    mapButton.onClick = [this]
    {
        showMap = true;
        gridButton.setToggleState(false, juce::dontSendNotification);
        mapButton.setToggleState(true, juce::dontSendNotification);
        map.setVisible(true);
        for (auto& card : candidateCards) card->setVisible(false);
        resized();
    };
    newButton.onClick = [this] { createNew(); };
    keptButton.setClickingTogglesState(true);
    keptButton.onClick = [this]
    {
        keptOnly = keptButton.getToggleState();
        updateResultVisibility();
        resized();
    };
    recentButton.onClick = [this] { setRecentVisible(true); };
    map.onCandidateSelected = [this](int rank) { selectCandidate(rank); };
    map.setMouseCursor(juce::MouseCursor::PointingHandCursor);

    for (int index = 0; index < 8; ++index)
    {
        auto& card = *candidateCards[static_cast<size_t>(index)];
        card.setAudio(juce::String(index + 1).paddedLeft('0', 2), "Waiting for audio", {});
        card.setDraggable(true);
        card.onActivate = [this, index] { selectCandidate(index + 1); };
        card.onSeek = [this, index](double position)
        {
            selectCandidate(index + 1, false);
            seekPreview(candidateCards[static_cast<size_t>(index)]->file(), position, index + 1);
        };
        card.onDrag = [this, index]
        {
            selectCandidate(index + 1, false);
            dragSelected();
        };
    }

    selectedTitle.setFont(DivergeTheme::monoBold(12.5f));
    configureSupportingLabel(candidateDetail, "Choose a variation to hear it");
    abButton.onClick = [this]
    {
        if (selectedCandidate <= 0) return;
        const auto position = audioProcessor.previewProgress();
        const auto target = playingSource
                                ? candidateCards[static_cast<size_t>(selectedCandidate - 1)]->file()
                                : audioSlots[0];
        if (audioProcessor.loadPreview(target, audioSlots[0]))
        {
            audioProcessor.seekPreview(position);
            audioProcessor.playPreview();
            playingCandidate = playingSource ? selectedCandidate : 0;
            playingSource = !playingSource;
            updateTransportUi();
        }
    };
    passButton.onClick = [this] { recordDecision(CandidateDecision::pass); };
    keepButton.onClick = [this] { recordDecision(CandidateDecision::keep); };
    favoriteButton.onClick = [this] { recordDecision(CandidateDecision::favorite); };
    branchButton.onClick = [this] { branchFromSelected(); };
    dragButton.onClick = [this] { dragSelected(); };
    tighterButton.onClick = [this] { adjustRange(-20); };
    widerButton.onClick = [this] { adjustRange(20); };
    keepButton.setColour(juce::TextButton::buttonColourId, DivergeTheme::decision);
    keepButton.setColour(juce::TextButton::textColourOffId, DivergeTheme::canvas);
    dragButton.setColour(juce::TextButton::buttonColourId, DivergeTheme::exploration);
    dragButton.setColour(juce::TextButton::textColourOffId, DivergeTheme::canvas);
    configureSupportingLabel(shortcutLabel, "SPACE play  /  ARROWS choose  /  A source  /  K keep  /  X pass  /  CMD-Z undo",
                             juce::Justification::centredRight);
    shortcutLabel.setFont(DivergeTheme::mono(10.0f));
    shortcutLabel.setColour(juce::Label::textColourId, DivergeTheme::dim);
    configureSupportingLabel(comparisonLabel, "Which direction is more you?");
    comparisonLabel.setFont(DivergeTheme::bodyBold(12.5f));
    comparisonAButton.onClick = [this] { recordComparison("prefer_a"); };
    comparisonBButton.onClick = [this] { recordComparison("prefer_b"); };
    comparisonNeitherButton.onClick = [this] { recordComparison("neither"); };
    comparisonSkipButton.onClick = [this] { skipComparison(); };
    comparisonLabel.setVisible(false);
    comparisonAButton.setVisible(false);
    comparisonBButton.setVisible(false);
    comparisonNeitherButton.setVisible(false);
    comparisonSkipButton.setVisible(false);

    recentPanel.setVisible(false);
    recentPanel.addAndMakeVisible(recentTitle);
    recentPanel.addAndMakeVisible(recentClose);
    recentTitle.setText("RECENT RUNS", juce::dontSendNotification);
    recentTitle.setFont(DivergeTheme::display(15.0f));
    recentClose.onClick = [this] { setRecentVisible(false); };
    for (int index = 0; index < static_cast<int>(recentCards.size()); ++index)
    {
        auto& card = *recentCards[static_cast<size_t>(index)];
        recentPanel.addAndMakeVisible(card);
        card.setAudio("Recent", "No saved run", {});
        card.onActivate = [this, index]
        {
            const auto run = recentRunDirectories[static_cast<size_t>(index)];
            if (run.isDirectory())
            {
                setRecentVisible(false);
                loadRun(run);
            }
        };
    }

    settingsButton.onClick = [this] { setSettingsVisible(true); };
    settingsPanel.setVisible(false);
    for (auto* component : std::initializer_list<juce::Component*> {
             &settingsTitle, &settingsSubtitle, &settingsClose, &studioStatus, &learningStatus,
             &libraryStatus, &opinionLabel, &opinionSlider, &opinionValue, &learningToggle,
             &calibrateButton, &resetTasteButton, &exportTasteButton, &importTasteButton,
             &advancedButton,
             &pythonLabel, &pythonEditor, &modelsLabel, &modelsEditor, &libraryLabel, &libraryEditor,
             &choicesLabel, &choicesEditor, &outputLabel, &outputEditor })
        settingsPanel.addAndMakeVisible(component);
    settingsTitle.setText("SETTINGS", juce::dontSendNotification);
    settingsTitle.setFont(DivergeTheme::display(17.0f));
    configureSupportingLabel(settingsSubtitle, "Diverge runs and learns entirely on this Mac.");
    settingsSubtitle.setColour(juce::Label::textColourId, DivergeTheme::dim);
    settingsClose.onClick = [this] { saveSettings(); setSettingsVisible(false); };
    studioStatus.set("Studio", "Local engine and models are checked before creation.",
                     StatusCard::State::neutral);
    learningStatus.set("Preferences", "Learns only from choices you make. Stored locally.",
                       StatusCard::State::neutral);
    libraryStatus.set("Library", "Library avoidance appears only after an index is connected.",
                      StatusCard::State::neutral);
    opinionLabel.setText("Opinion", juce::dontSendNotification);
    opinionLabel.setFont(DivergeTheme::bodyBold(12.5f));
    opinionLabel.setColour(juce::Label::textColourId, DivergeTheme::muted);
    opinionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    opinionSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    opinionSlider.setRange(0.0, 100.0, 1.0);
    opinionSlider.setValue(50.0);
    opinionSlider.onValueChange = [this]
    {
        opinionValue.setText(juce::String(static_cast<int>(opinionSlider.getValue())) + "%",
                             juce::dontSendNotification);
        workflow.opinion = static_cast<int>(opinionSlider.getValue());
    };
    configureSupportingLabel(opinionValue, "50%", juce::Justification::centredRight);
    learningToggle.setToggleState(true, juce::dontSendNotification);
    learningToggle.onClick = [this]
    {
        workflow.learningEnabled = learningToggle.getToggleState();
        runCriticCommand({ "set-learning", workflow.learningEnabled ? "enabled" : "disabled",
                           "--events", tasteEventsFile().getFullPathName() });
        saveSettings();
    };
    calibrateButton.onClick = [this] { beginCalibration(); };
    resetTasteButton.onClick = [this] { resetTasteProfile(); };
    exportTasteButton.onClick = [this] { exportTasteProfile(); };
    importTasteButton.onClick = [this] { importTasteProfile(); };
    advancedButton.onClick = [this] { setAdvancedVisible(!showAdvanced); };
    pythonLabel.setText("Engine", juce::dontSendNotification);
    modelsLabel.setText("Models", juce::dontSendNotification);
    libraryLabel.setText("Library index", juce::dontSendNotification);
    choicesLabel.setText("Choices", juce::dontSendNotification);
    outputLabel.setText("Storage", juce::dontSendNotification);
    for (auto* label : { &pythonLabel, &modelsLabel, &libraryLabel, &choicesLabel, &outputLabel })
    {
        label->setFont(DivergeTheme::bodyBold(12.5f));
        label->setColour(juce::Label::textColourId, DivergeTheme::muted);
    }
    for (auto* editor : { &pythonEditor, &modelsEditor, &libraryEditor, &choicesEditor, &outputEditor })
    {
        editor->setFont(DivergeTheme::mono(12.0f));
        editor->onFocusLost = [this] { saveSettings(); };
    }
    setAdvancedVisible(false);
}

void DivergeAudioProcessorEditor::renderBackground()
{
    const auto width = getWidth();
    const auto height = getHeight();
    if (width <= 0 || height <= 0) return;
    backgroundImage = juce::Image(juce::Image::ARGB, width, height, true);
    juce::Graphics g(backgroundImage);
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);

    g.setGradientFill({ DivergeTheme::canvasHi, 0.0f, 0.0f, DivergeTheme::canvas, 0.0f, h, false });
    g.fillAll();

    // Soft accent bloom behind the header.
    g.setGradientFill(juce::ColourGradient(DivergeTheme::exploration.withAlpha(0.05f), 110.0f, 30.0f,
                                           juce::Colours::transparentBlack, w * 0.62f, h * 0.72f, true));
    g.fillRect(0, 0, width, height);

    // Divergence rays fanning out from the brand mark.
    const juce::Point<float> origin(30.0f, 44.0f);
    for (int ray = 0; ray < 7; ++ray)
    {
        const auto angle = juce::degreesToRadians(74.0f + static_cast<float>(ray) * 6.5f);
        const auto end = origin.getPointOnCircumference(w * 1.4f, angle);
        g.setGradientFill(juce::ColourGradient(DivergeTheme::exploration.withAlpha(0.05f), origin.x, origin.y,
                                               juce::Colours::transparentBlack, end.x, end.y, false));
        g.drawLine({ origin, end }, 1.0f);
    }

    // Vignette keeps focus in the working area.
    juce::ColourGradient vignette(juce::Colours::transparentBlack, w * 0.5f, h * 0.42f,
                                  juce::Colour(0xff05070a).withAlpha(0.5f), 0.0f, h, true);
    g.setGradientFill(vignette);
    g.fillRect(0, 0, width, height);
}

void DivergeAudioProcessorEditor::paint(juce::Graphics& g)
{
    if (backgroundImage.isValid()) g.drawImageAt(backgroundImage, 0, 0);
    else g.fillAll(DivergeTheme::canvas);

    // Brand mark: three strokes diverging from one origin.
    {
        const juce::Point<float> origin(26.0f, 44.0f);
        g.setColour(DivergeTheme::exploration);
        g.drawLine({ origin, { 42.0f, 36.0f } }, 1.8f);
        g.drawLine({ origin, { 44.0f, 44.0f } }, 1.8f);
        g.drawLine({ origin, { 42.0f, 52.0f } }, 1.8f);
        g.fillEllipse(juce::Rectangle<float>(4.5f, 4.5f).withCentre(origin));
    }
    g.setColour(DivergeTheme::hairline.withAlpha(0.9f));
    g.fillRect(24, 71, getWidth() - 48, 1);

    if (localBadge.isVisible())
    {
        const auto pill = localBadge.getBounds().toFloat().reduced(0.0f, 9.0f);
        g.setColour(DivergeTheme::explorationSoft.withAlpha(0.75f));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        g.setColour(DivergeTheme::exploration.withAlpha(0.4f));
        g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f);
    }

    const auto generating = audioProcessor.generation().isActive() || fixtureMode == "generating";
    if (generating && progressLabel.isVisible())
    {
        auto zone = progressLabel.getBounds().toFloat();
        auto bar = zone.removeFromBottom(5.0f)
                       .withSizeKeepingCentre(juce::jmin(360.0f, zone.getWidth() - 40.0f), 5.0f);
        g.setColour(DivergeTheme::hairline.brighter(0.05f));
        g.fillRoundedRectangle(bar, 2.5f);
        const auto fillWidth = juce::jmax(5.0f, bar.getWidth() * displayedProgress);
        auto fill = bar.withWidth(fillWidth);
        g.setGradientFill({ DivergeTheme::exploration.darker(0.3f), fill.getX(), fill.getY(),
                            DivergeTheme::exploration, fill.getRight(), fill.getY(), false });
        g.fillRoundedRectangle(fill, 2.5f);
        g.setColour(DivergeTheme::exploration.withAlpha(0.35f));
        g.fillEllipse(juce::Rectangle<float>(11.0f, 11.0f).withCentre({ fill.getRight(), fill.getCentreY() }));
        g.setColour(DivergeTheme::text);
        g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre({ fill.getRight(), fill.getCentreY() }));
        if (displayedProgress < 0.08f && !DivergeTheme::reducedMotion())
        {
            // Indeterminate shimmer while the engine warms up.
            const auto phase = static_cast<float>(std::fmod(
                juce::Time::getMillisecondCounterHiRes() * 0.00055, 1.0));
            const auto sweepX = bar.getX() + phase * (bar.getWidth() - 60.0f);
            g.setGradientFill({ juce::Colours::transparentBlack, sweepX, 0.0f,
                                DivergeTheme::exploration.withAlpha(0.35f), sweepX + 30.0f, 0.0f, false });
            g.fillRoundedRectangle(juce::Rectangle<float>(sweepX, bar.getY(), 60.0f, bar.getHeight()), 2.5f);
        }
    }
}

void DivergeAudioProcessorEditor::paintOverChildren(juce::Graphics& g)
{
    if (dragHover)
    {
        const auto zone = getLocalBounds().toFloat().reduced(10.0f);
        g.setColour(DivergeTheme::exploration.withAlpha(0.08f));
        g.fillRoundedRectangle(zone, 14.0f);
        juce::Path outline;
        outline.addRoundedRectangle(zone, 14.0f);
        juce::Path dashed;
        const float dashes[] = { 8.0f, 6.0f };
        juce::PathStrokeType(2.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(DivergeTheme::exploration);
        g.fillPath(dashed);
        const auto hint = getLocalBounds().withSizeKeepingCentre(300, 44).toFloat();
        g.setColour(DivergeTheme::raised.withAlpha(0.96f));
        g.fillRoundedRectangle(hint, 22.0f);
        g.setColour(DivergeTheme::exploration.withAlpha(0.6f));
        g.drawRoundedRectangle(hint, 22.0f, 1.2f);
        g.setColour(DivergeTheme::text);
        g.setFont(DivergeTheme::bodyBold(14.0f));
        g.drawText("Drop to set your source", hint, juce::Justification::centred, false);
    }
    if (transitionAlpha > 0.0f && transitionImage.isValid())
    {
        g.setOpacity(transitionAlpha);
        g.drawImageAt(transitionImage, 0, 0);
        g.setOpacity(1.0f);
    }
}

void DivergeAudioProcessorEditor::resized()
{
    if (getWidth() != backgroundImage.getWidth() || getHeight() != backgroundImage.getHeight())
        renderBackground();

    scrim.setBounds(getLocalBounds());
    const auto toastY = (!showPrepare && !settingsPanel.isVisible())
                            ? getHeight() - 24 - 154 - 64
                            : getHeight() - 84;
    toast.setBounds(getLocalBounds()
                        .withSizeKeepingCentre(juce::jmin(620, getWidth() - 96), 54)
                        .withY(toastY));

    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(40);
    header.removeFromLeft(26);
    brandLabel.setBounds(header.removeFromLeft(126));
    promiseLabel.setBounds(header.removeFromLeft(446));
    settingsButton.setBounds(header.removeFromRight(44));
    header.removeFromRight(8);
    localBadge.setBounds(header.removeFromRight(82));
    area.removeFromTop(14);

    const auto drawerWidth = juce::jmin(460, getWidth() - 72);
    recentTargetBounds = { getWidth() - drawerWidth - 24, 132, drawerWidth, getHeight() - 156 };
    recentPanel.setSize(recentTargetBounds.getWidth(), recentTargetBounds.getHeight());
    positionRecentPanel();
    {
        auto recent = recentPanel.getLocalBounds().reduced(18);
        auto recentHeader = recent.removeFromTop(40);
        recentTitle.setBounds(recentHeader.removeFromLeft(220));
        recentClose.setBounds(recentHeader.removeFromRight(74));
        recent.removeFromTop(10);
        const auto count = static_cast<int>(recentCards.size());
        const auto cardHeight = juce::jmax(72, (recent.getHeight() - 8 * (count - 1)) / count);
        for (auto& card : recentCards)
        {
            card->setBounds(recent.removeFromTop(cardHeight));
            recent.removeFromTop(8);
        }
    }

    if (settingsPanel.isVisible())
    {
        settingsPanel.setBounds(area);
        auto settings = settingsPanel.getLocalBounds().reduced(28, 24);
        settings = settings.withSizeKeepingCentre(juce::jmin(880, settings.getWidth()), settings.getHeight());
        auto titleRow = settings.removeFromTop(36);
        settingsClose.setBounds(titleRow.removeFromRight(88).reduced(0, 0));
        settingsTitle.setBounds(titleRow.removeFromLeft(240));
        settingsSubtitle.setBounds(settings.removeFromTop(22));
        settings.removeFromTop(18);
        auto cards = settings.removeFromTop(104);
        const auto statusWidth = (cards.getWidth() - 24) / 3;
        studioStatus.setBounds(cards.removeFromLeft(statusWidth)); cards.removeFromLeft(12);
        learningStatus.setBounds(cards.removeFromLeft(statusWidth)); cards.removeFromLeft(12);
        libraryStatus.setBounds(cards);
        settings.removeFromTop(16);
        auto opinion = settings.removeFromTop(34);
        opinionLabel.setBounds(opinion.removeFromLeft(72));
        opinionValue.setBounds(opinion.removeFromRight(54));
        opinionSlider.setBounds(opinion);
        auto profileActions = settings.removeFromTop(38);
        learningToggle.setBounds(profileActions.removeFromLeft(230));
        profileActions.removeFromLeft(10);
        calibrateButton.setBounds(profileActions.removeFromLeft(128));
        profileActions.removeFromLeft(8);
        resetTasteButton.setBounds(profileActions.removeFromLeft(72));
        profileActions.removeFromLeft(8);
        exportTasteButton.setBounds(profileActions.removeFromLeft(72));
        profileActions.removeFromLeft(8);
        importTasteButton.setBounds(profileActions.removeFromLeft(72));
        settings.removeFromTop(12);
        advancedButton.setBounds(settings.removeFromTop(40).removeFromLeft(190));
        settings.removeFromTop(14);
        auto place = [&settings](juce::Label& label, juce::TextEditor& editor)
        {
            auto row = settings.removeFromTop(40);
            label.setBounds(row.removeFromLeft(110));
            editor.setBounds(row);
            settings.removeFromTop(8);
        };
        place(pythonLabel, pythonEditor); place(modelsLabel, modelsEditor); place(libraryLabel, libraryEditor);
        place(choicesLabel, choicesEditor); place(outputLabel, outputEditor);
        settingsPanel.toFront(false);
        return;
    }

    if (showPrepare)
    {
        const auto contentWidth = juce::jmin(940, area.getWidth());
        area = area.withSizeKeepingCentre(contentWidth, area.getHeight());
        sourceSection.setBounds(area.removeFromTop(22));
        auto source = area.removeFromTop(104);
        recordButton.setBounds(source.removeFromRight(112).reduced(10, 28));
        source.removeFromRight(8);
        sourceCard->setBounds(source);
        area.removeFromTop(14);
        auto directionHeader = area.removeFromTop(30);
        directionSection.setBounds(directionHeader.removeFromLeft(220));
        addDirectionButton.setBounds(directionHeader.removeFromRight(132));
        if (audioSlots[1].existsAsFile())
        {
            directionHeader.removeFromRight(6);
            removeDirectionButton.setBounds(directionHeader.removeFromRight(82));
            directionHeader.removeFromRight(6);
            replaceDirectionButton.setBounds(directionHeader.removeFromRight(82));
        }
        directionCard->setBounds(area.removeFromTop(84));
        if (showDirectionText)
        {
            area.removeFromTop(8);
            styleEditor.setBounds(area.removeFromTop(44));
        }
        area.removeFromTop(18);
        auto intent = area.removeFromTop(112);
        auto change = intent.removeFromLeft((intent.getWidth() - 28) / 2);
        intent.removeFromLeft(28);
        changeSection.setBounds(change.removeFromTop(22));
        auto endpoints = change.removeFromBottom(20);
        familiarLabel.setBounds(endpoints.removeFromLeft(endpoints.getWidth() / 2));
        wildLabel.setBounds(endpoints);
        changeSlider.setBounds(change);
        preserveSection.setBounds(intent.removeFromTop(22));
        auto locks = intent.removeFromTop(44);
        const auto lockWidth = (locks.getWidth() - 16) / 3;
        grooveLock.setBounds(locks.removeFromLeft(lockWidth)); locks.removeFromLeft(8);
        melodyLock.setBounds(locks.removeFromLeft(lockWidth)); locks.removeFromLeft(8);
        timbreLock.setBounds(locks);
        area.removeFromTop(18);
        auto action = area.removeFromTop(52);
        generateButton.setBounds(action.withSizeKeepingCentre(250, 52));
        cancelButton.setBounds(action.removeFromRight(100).reduced(4, 6));
        progressLabel.setBounds(area.removeFromTop(38));
        privacyLabel.setBounds(area.removeFromTop(24));
    }
    else
    {
        auto toolbar = area.removeFromTop(42);
        briefButton.setBounds(toolbar.removeFromLeft(88));
        resultsTitle.setBounds(toolbar.removeFromLeft(150));
        newButton.setBounds(toolbar.removeFromRight(110)); toolbar.removeFromRight(8);
        recentButton.setBounds(toolbar.removeFromRight(76)); toolbar.removeFromRight(6);
        mapButton.setBounds(toolbar.removeFromRight(64)); toolbar.removeFromRight(6);
        gridButton.setBounds(toolbar.removeFromRight(64));
        toolbar.removeFromRight(6);
        keptButton.setBounds(toolbar.removeFromRight(68));
        area.removeFromTop(10);

        auto selected = area.removeFromBottom(comparisonVisible ? 196 : 154);
        area.removeFromBottom(10);
        map.setBounds(area);
        if (!showMap)
        {
            const auto columns = getWidth() >= 1260 ? 4 : 2;
            std::vector<int> visible;
            for (int index = 0; index < 8; ++index)
                if (candidateCards[static_cast<size_t>(index)]->isVisible()) visible.push_back(index);
            const auto rows = juce::jmax(1, (static_cast<int>(visible.size()) + columns - 1) / columns);
            const auto gap = 8;
            const auto cardWidth = (area.getWidth() - gap * (columns - 1)) / columns;
            const auto cardHeight = (area.getHeight() - gap * (rows - 1)) / rows;
            for (int position = 0; position < static_cast<int>(visible.size()); ++position)
            {
                const auto column = position % columns;
                const auto row = position / columns;
                candidateCards[static_cast<size_t>(visible[static_cast<size_t>(position)])]->setBounds(
                    area.getX() + column * (cardWidth + gap), area.getY() + row * (cardHeight + gap),
                    cardWidth, cardHeight);
            }
        }

        auto detail = selected.removeFromTop(44);
        selectedTitle.setBounds(detail.removeFromLeft(160));
        candidateDetail.setBounds(detail);
        auto actions = selected.removeFromTop(48);
        abButton.setBounds(actions.removeFromLeft(100)); actions.removeFromLeft(6);
        passButton.setBounds(actions.removeFromLeft(74)); actions.removeFromLeft(6);
        keepButton.setBounds(actions.removeFromLeft(74)); actions.removeFromLeft(6);
        favoriteButton.setBounds(actions.removeFromLeft(108)); actions.removeFromLeft(12);
        dragButton.setBounds(actions.removeFromRight(142)); actions.removeFromRight(6);
        branchButton.setBounds(actions.removeFromRight(128));
        auto secondary = selected.removeFromTop(38);
        tighterButton.setBounds(secondary.removeFromLeft(104)); secondary.removeFromLeft(6);
        widerButton.setBounds(secondary.removeFromLeft(104));
        shortcutLabel.setBounds(secondary);
        if (comparisonVisible)
        {
            auto comparison = selected.removeFromTop(40);
            comparisonLabel.setBounds(comparison.removeFromLeft(260));
            comparisonAButton.setBounds(comparison.removeFromLeft(112)); comparison.removeFromLeft(6);
            comparisonBButton.setBounds(comparison.removeFromLeft(112)); comparison.removeFromLeft(6);
            comparisonNeitherButton.setBounds(comparison.removeFromLeft(82)); comparison.removeFromLeft(6);
            comparisonSkipButton.setBounds(comparison.removeFromLeft(66));
        }
    }
}

void DivergeAudioProcessorEditor::beginViewTransition()
{
    if (DivergeTheme::reducedMotion() || getWidth() <= 0 || !isShowing()) return;
    transitionImage = createComponentSnapshot(getLocalBounds());
    transitionAlpha = 1.0f;
}

void DivergeAudioProcessorEditor::setPrepareVisible(bool visible)
{
    if (showPrepare != visible) beginViewTransition();
    showPrepare = visible;
    if (visible) closeRecentImmediately();
    const auto generating = audioProcessor.generation().isActive();
    for (auto* component : std::initializer_list<juce::Component*> {
             &sourceSection, sourceCard.get(), &recordButton, &directionSection, directionCard.get(),
             &replaceDirectionButton, &removeDirectionButton, &addDirectionButton,
             &changeSection, &changeSlider, &familiarLabel, &wildLabel, &preserveSection,
             &grooveLock, &melodyLock, &timbreLock, &generateButton, &progressLabel, &privacyLabel })
        component->setVisible(visible);
    refreshSlotCard(1);
    styleEditor.setVisible(visible && showDirectionText);
    cancelButton.setVisible(visible && generating);
    for (auto* component : std::initializer_list<juce::Component*> {
             &briefButton, &resultsTitle, &gridButton, &mapButton, &keptButton, &recentButton, &newButton, &map, &selectedTitle,
             &candidateDetail, &abButton, &passButton, &keepButton, &favoriteButton, &branchButton,
             &dragButton, &tighterButton, &widerButton, &shortcutLabel, &comparisonLabel,
             &comparisonAButton, &comparisonBButton, &comparisonNeitherButton,
             &comparisonSkipButton })
        component->setVisible(!visible);
    comparisonLabel.setVisible(!visible && comparisonVisible);
    comparisonAButton.setVisible(!visible && comparisonVisible);
    comparisonBButton.setVisible(!visible && comparisonVisible);
    comparisonNeitherButton.setVisible(!visible && comparisonVisible);
    comparisonSkipButton.setVisible(!visible && comparisonVisible);
    for (auto& card : candidateCards) card->setVisible(!visible && !showMap);
    if (!visible) updateResultVisibility();
    map.setVisible(!visible && showMap);
    resized();
    repaint();
}

void DivergeAudioProcessorEditor::setSettingsVisible(bool visible)
{
    if (settingsPanel.isVisible() != visible) beginViewTransition();
    if (visible) closeRecentImmediately();
    settingsPanel.setVisible(visible);
    if (visible)
    {
        opinionSlider.setValue(workflow.opinion, juce::dontSendNotification);
        opinionValue.setText(juce::String(workflow.opinion) + "%", juce::dontSendNotification);
        learningToggle.setToggleState(workflow.learningEnabled, juce::dontSendNotification);
        settingsPanel.toFront(false);
    }
    resized();
    repaint();
}

void DivergeAudioProcessorEditor::setAdvancedVisible(bool visible)
{
    showAdvanced = visible;
    advancedButton.setButtonText(visible ? "Hide advanced" : "Advanced diagnostics");
    for (auto* component : std::initializer_list<juce::Component*> {
             &pythonLabel, &pythonEditor, &modelsLabel, &modelsEditor, &libraryLabel, &libraryEditor,
             &choicesLabel, &choicesEditor, &outputLabel, &outputEditor })
        component->setVisible(visible);
    resized();
}

bool DivergeAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;wave;aif;aiff;flac;mp3;m4a");
}

void DivergeAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int, int)
{
    dragHover = isInterestedInFileDrag(files);
    repaint();
}

void DivergeAudioProcessorEditor::fileDragExit(const juce::StringArray&)
{
    dragHover = false;
    repaint();
}

void DivergeAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    dragHover = false;
    if (files.size() == 1)
        setAudioSlot(directionCard->getBounds().contains(x, y) ? 1 : 0, juce::File(files[0]));
    repaint();
}

void DivergeAudioProcessorEditor::chooseAudio(int slot)
{
    chooser = std::make_unique<juce::FileChooser>(slot == 0 ? "Choose a source" : "Choose a direction",
                                                   juce::File {}, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
    juce::Component::SafePointer<DivergeAudioProcessorEditor> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [safeThis, slot](const juce::FileChooser& selected)
                         {
                             if (safeThis != nullptr && selected.getResult().existsAsFile())
                                 safeThis->setAudioSlot(slot, selected.getResult());
                         });
}

void DivergeAudioProcessorEditor::refreshSlotCard(int slot)
{
    const auto& file = audioSlots[static_cast<size_t>(slot)];
    const auto heading = file.existsAsFile() ? file.getFileName() : juce::String();
    if (slot == 0)
        sourceCard->setAudio(heading, "Drop, record, or choose a source", file);
    else if (slot == 1)
    {
        directionCard->setAudio(heading, "Add an optional reference to pull toward", file);
        const auto showActions = showPrepare && file.existsAsFile();
        replaceDirectionButton.setVisible(showActions);
        removeDirectionButton.setVisible(showActions);
    }
}

void DivergeAudioProcessorEditor::setAudioSlot(int slot, const juce::File& file)
{
    audioSlots[static_cast<size_t>(slot)] = file;
    workflow.audioSlots[static_cast<size_t>(slot)] = file;
    if (slot <= 1)
    {
        refreshSlotCard(slot);
        if (slot == 1) resized();
    }
    workflow.view = audioSlots[0].existsAsFile() ? WorkflowViewState::ready : WorkflowViewState::needsSetup;
    saveSettings();
}

void DivergeAudioProcessorEditor::toggleCapture()
{
    if (!audioProcessor.isCapturing())
    {
        audioProcessor.beginCapture();
        recordButton.setButtonText("Stop");
        showToast("Recording - up to 30 seconds");
    }
    else
    {
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("diverge-capture", ".wav");
        if (audioProcessor.finishCapture(file)) setAudioSlot(0, file);
        recordButton.setButtonText("Record");
    }
}

void DivergeAudioProcessorEditor::togglePreview(const juce::File& file, int candidateRank, bool source)
{
    if (!file.existsAsFile()) return;
    const auto sameFile = audioProcessor.previewPath() == file.getFullPathName();
    if (sameFile && audioProcessor.isPreviewPlaying())
    {
        audioProcessor.stopPreview();
        playingCandidate = 0;
        playingSource = false;
    }
    else if (audioProcessor.loadPreview(file, audioSlots[0]))
    {
        audioProcessor.playPreview();
        playingCandidate = candidateRank;
        playingSource = source;
    }
    updateTransportUi();
}

void DivergeAudioProcessorEditor::seekPreview(const juce::File& file, double proportion,
                                               int candidateRank, bool source)
{
    if (!file.existsAsFile()) return;
    if (audioProcessor.previewPath() != file.getFullPathName()
        && !audioProcessor.loadPreview(file, audioSlots[0])) return;
    audioProcessor.seekPreview(proportion);
    audioProcessor.playPreview();
    playingCandidate = candidateRank;
    playingSource = source;
    updateTransportUi();
}

void DivergeAudioProcessorEditor::updateTransportUi()
{
    const auto playing = audioProcessor.isPreviewPlaying();
    const auto path = audioProcessor.previewPath();
    const auto progress = playing ? audioProcessor.previewProgress() : 0.0;
    sourceCard->setState(false, playing && path == audioSlots[0].getFullPathName(), progress, CandidateDecision::none);
    directionCard->setState(false, playing && path == audioSlots[1].getFullPathName(), progress, CandidateDecision::none);
    for (int index = 0; index < 8; ++index)
    {
        auto& card = *candidateCards[static_cast<size_t>(index)];
        card.setState(index + 1 == selectedCandidate, playing && path == card.file().getFullPathName(), progress,
                      workflow.decisions[static_cast<size_t>(index)]);
        card.advanceAnimation();
    }
    map.setDecisions(workflow.decisions);
    abButton.setButtonText(playingSource ? "Hear result" : "A/B Source");
}

juce::String DivergeAudioProcessorEditor::styleHint() const { return styleEditor.getText().trim(); }

juce::File DivergeAudioProcessorEditor::writeRunConfig() const
{
    auto root = juce::JSON::parse("{}");
    auto* object = root.getDynamicObject();
    object->setProperty("source", audioSlots[0].getFullPathName());
    juce::Array<juce::var> references;
    for (int index = 0; index < 2; ++index)
        if (audioSlots[static_cast<size_t>(index + 1)].existsAsFile())
        {
            juce::Array<juce::var> reference;
            reference.add(audioSlots[static_cast<size_t>(index + 1)].getFullPathName());
            reference.add(index == 0 ? 1.0 : 0.5);
            references.add(reference);
        }
    object->setProperty("references", references);
    object->setProperty("transform", static_cast<int>(changeSlider.getValue()));
    object->setProperty("spread", workflow.range);
    object->setProperty("drift", libraryEditor.getText().trim().isNotEmpty() ? 35 : 0);
    juce::Array<juce::var> locks;
    if (grooveLock.getToggleState()) locks.add("groove");
    if (melodyLock.getToggleState()) locks.add("melody");
    if (timbreLock.getToggleState()) locks.add("timbre");
    object->setProperty("locks", locks);
    object->setProperty("n_return", 8);
    object->setProperty("n_oversample", 16);
    object->setProperty("seed", juce::Random::getSystemRandom().nextInt());
    object->setProperty("library_index", libraryEditor.getText().trim());
    object->setProperty("critic_model", juce::File(modelsEditor.getText()).getChildFile("critic.joblib").getFullPathName());
    object->setProperty("choices_path", choicesFile().getFullPathName());
    object->setProperty("taste_events_path", tasteEventsFile().getFullPathName());
    object->setProperty("taste_model_path", tasteModelFile().getFullPathName());
    object->setProperty("opinion", static_cast<int>(opinionSlider.getValue()));
    object->setProperty("taste_learning_enabled", learningToggle.getToggleState());
    object->setProperty("style_text_hint", styleHint());
    object->setProperty("lock_threshold", 0.55);
    object->setProperty("fast", true);
    object->setProperty("generation_batch_size", 8);
    object->setProperty("self_novelty_weight", 0.05);
    object->setProperty("output_dir", outputEditor.getText().trim());
    const auto parentRun = audioProcessor.state().getProperty("pendingParentRun", {}).toString();
    if (parentRun.isNotEmpty())
    {
        object->setProperty("parent_run_id", parentRun);
        object->setProperty("parent_candidate", audioProcessor.state().getProperty("pendingParentCandidate", 0));
    }
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("diverge-run", ".json");
    file.replaceWithText(juce::JSON::toString(root, true));
    return file;
}

void DivergeAudioProcessorEditor::startGeneration()
{
    if (!audioSlots[0].existsAsFile())
    {
        showToast("Choose, drop, or record a source first");
        sourceCard->grabKeyboardFocus();
        return;
    }
    const auto python = juce::File(pythonEditor.getText().trim());
    const auto models = juce::File(modelsEditor.getText().trim());
    if (!python.existsAsFile() || !models.isDirectory())
    {
        showToast("Local engine setup needs attention - open Settings");
        return;
    }
    saveSettings();
    const auto config = writeRunConfig();
    const juce::StringArray command { pythonEditor.getText().trim(), "-m", "diverge.cli", "run", "--config",
                                      quotedPath(config), "--models-dir", modelsEditor.getText().trim() };
    if (!audioProcessor.generation().start(command, juce::File(outputEditor.getText().trim()))) return;
    workflow.view = WorkflowViewState::generating;
    generateButton.setEnabled(false);
    generateButton.setButtonText("Creating...");
    cancelButton.setVisible(true);
    progressLabel.setColour(juce::Label::textColourId, DivergeTheme::muted);
    progressLabel.setText("Preparing your source", juce::dontSendNotification);
    displayedProgress = 0.02f;
    repaint();
}

void DivergeAudioProcessorEditor::loadRun(const juce::File& run)
{
    const auto sameRun = workflow.activeRunId == run.getFileName();
    loadedRun = RunModel::load(run);
    if (!loadedRun.isValid())
    {
        showToast("The completed run could not be loaded - its bundle is still safe");
        return;
    }
    currentRun = run;
    restoreRunDecisions(sameRun);
    map.setPoints(loadedRun.mapPoints);
    for (int index = 0; index < 8; ++index)
    {
        auto& card = *candidateCards[static_cast<size_t>(index)];
        if (const auto* candidate = loadedRun.candidate(index + 1))
        {
            card.setAudio(juce::String(index + 1).paddedLeft('0', 2), "Missing audio", candidate->file);
            card.setSupportingText(candidate->explanation);
            card.setDraggable(true);
            if (!sameRun && isShowing()) card.beginEntrance(index);
        }
    }
    workflow.activeRunId = run.getFileName();
    workflow.view = WorkflowViewState::results;
    totalChoiceCount = loadedRun.tasteObservations;
    tasteConfidence = loadedRun.tasteConfidence;
    opinionSlider.setValue(loadedRun.opinion, juce::dontSendNotification);
    opinionValue.setText(juce::String(loadedRun.opinion) + "%", juce::dontSendNotification);
    comparisonsAsked.clear();
    comparisonsRemaining = 1;
    chooseNextComparison();
    const auto restore = workflow.selectedCandidate >= 1 ? workflow.selectedCandidate : 1;
    setPrepareVisible(false);
    selectCandidate(restore, false);
    if (isShowing()) candidateCards[static_cast<size_t>(restore - 1)]->grabKeyboardFocus();
    showToast("Eight variations ready - click one to hear it");
    saveSettings();
}

void DivergeAudioProcessorEditor::updateTasteProfile(const juce::var& status)
{
    totalChoiceCount = static_cast<int>(status.getProperty(
        "observations", status.getProperty("events", totalChoiceCount)));
    tasteConfidence = static_cast<double>(status.getProperty("confidence", tasteConfidence));
    positiveTasteModes = static_cast<int>(status.getProperty("positive_modes", positiveTasteModes));
    negativeTasteModes = static_cast<int>(status.getProperty("negative_modes", negativeTasteModes));
    const auto confidencePercent = static_cast<int>(std::round(tasteConfidence * 100.0));
    const auto modes = positiveTasteModes + negativeTasteModes;
    learningStatus.set(
        "Preferences",
        totalChoiceCount > 0
            ? juce::String(totalChoiceCount) + " events  /  " + juce::String(confidencePercent)
                  + "% confidence  /  " + juce::String(modes) + " modes"
            : "Ready to learn from explicit Keeps, Passes, and Favorites.",
        totalChoiceCount > 0 ? StatusCard::State::ok : StatusCard::State::neutral);
}

void DivergeAudioProcessorEditor::resetTasteProfile()
{
    runCriticCommand({ "reset", "--events", tasteEventsFile().getFullPathName(),
                       "--model", tasteModelFile().getFullPathName() });
    comparisonsAsked.clear();
    showToast("Taste reset recorded - event history remains recoverable");
}

void DivergeAudioProcessorEditor::exportTasteProfile()
{
    const auto output = tasteModelFile().getSiblingFile("profile.joblib");
    runCriticCommand({ "export-model", "--events", tasteEventsFile().getFullPathName(),
                       "--model", tasteModelFile().getFullPathName(), "--output",
                       output.getFullPathName() });
    showToast("Portable profile exported beside your taste model");
}

void DivergeAudioProcessorEditor::importTasteProfile()
{
    chooser = std::make_unique<juce::FileChooser>("Import a Diverge taste profile", juce::File {}, "*.joblib");
    juce::Component::SafePointer<DivergeAudioProcessorEditor> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                         [safeThis](const juce::FileChooser& selected)
                         {
                             if (safeThis == nullptr || !selected.getResult().existsAsFile()) return;
                             safeThis->runCriticCommand(
                                 { "import-model", selected.getResult().getFullPathName(), "--events",
                                   safeThis->tasteEventsFile().getFullPathName(), "--model",
                                   safeThis->tasteModelFile().getFullPathName() });
                             safeThis->showToast("Taste profile imported and validated");
                         });
}

void DivergeAudioProcessorEditor::beginCalibration()
{
    if (!loadedRun.isValid())
    {
        showToast("Create a batch before starting calibration");
        return;
    }
    comparisonsAsked.clear();
    comparisonsRemaining = 6;
    setSettingsVisible(false);
    setPrepareVisible(false);
    chooseNextComparison();
    showToast("Six-comparison taste calibration started");
}

void DivergeAudioProcessorEditor::chooseNextComparison()
{
    comparisonA = 0;
    comparisonB = 0;
    double bestScore = -1.0;
    for (const auto& a : loadedRun.candidates)
        for (const auto& b : loadedRun.candidates)
        {
            if (a.rank >= b.rank) continue;
            const auto asked = std::find(comparisonsAsked.begin(), comparisonsAsked.end(),
                                         std::pair<int, int> { a.rank, b.rank });
            if (asked != comparisonsAsked.end()) continue;
            const auto boundary = 1.0 - juce::jmin(1.0, std::abs(a.taste - b.taste) * 2.0);
            const auto uncertainty = (a.tasteUncertainty + b.tasteUncertainty) * 0.5;
            const auto differentMode = a.tasteMode.isNotEmpty() && b.tasteMode.isNotEmpty()
                                       && a.tasteMode != b.tasteMode ? 0.15 : 0.0;
            const auto score = 0.55 * uncertainty + 0.30 * boundary + differentMode;
            if (score > bestScore)
            {
                bestScore = score;
                comparisonA = a.rank;
                comparisonB = b.rank;
            }
        }
    comparisonVisible = comparisonsRemaining > 0 && comparisonA > 0 && comparisonB > 0;
    if (comparisonVisible)
    {
        const auto prefix = comparisonsRemaining > 1
                                ? "Calibration " + juce::String(7 - comparisonsRemaining) + "/6  /  "
                                : juce::String();
        comparisonLabel.setText(prefix + "Which is more you: "
                                    + juce::String(comparisonA).paddedLeft('0', 2) + " or "
                                    + juce::String(comparisonB).paddedLeft('0', 2) + "?",
                                juce::dontSendNotification);
        comparisonAButton.setButtonText("A  " + juce::String(comparisonA).paddedLeft('0', 2));
        comparisonBButton.setButtonText("B  " + juce::String(comparisonB).paddedLeft('0', 2));
    }
    for (auto* component : { static_cast<juce::Component*>(&comparisonLabel),
                             static_cast<juce::Component*>(&comparisonAButton),
                             static_cast<juce::Component*>(&comparisonBButton),
                             static_cast<juce::Component*>(&comparisonNeitherButton),
                             static_cast<juce::Component*>(&comparisonSkipButton) })
        component->setVisible(!showPrepare && comparisonVisible);
    resized();
}

void DivergeAudioProcessorEditor::recordComparison(const juce::String& label)
{
    const auto* a = loadedRun.candidate(comparisonA);
    const auto* b = loadedRun.candidate(comparisonB);
    if (!comparisonVisible || a == nullptr || b == nullptr) return;
    comparisonsAsked.emplace_back(comparisonA, comparisonB);
    runCriticCommand({ "compare", a->file.getFullPathName(), b->file.getFullPathName(), label,
                       "--events", tasteEventsFile().getFullPathName(), "--model",
                       tasteModelFile().getFullPathName(), "--models-dir", modelsEditor.getText().trim(),
                       "--batch-id", currentRun.getFileName() });
    --comparisonsRemaining;
    chooseNextComparison();
    showToast("Comparison recorded - taste confidence will update locally");
}

void DivergeAudioProcessorEditor::skipComparison()
{
    if (!comparisonVisible) return;
    comparisonsAsked.emplace_back(comparisonA, comparisonB);
    --comparisonsRemaining;
    chooseNextComparison();
}

void DivergeAudioProcessorEditor::selectCandidate(int rank, bool playImmediately)
{
    const auto* candidate = loadedRun.candidate(rank);
    if (candidate == nullptr || !candidate->file.existsAsFile()) return;
    selectedCandidate = rank;
    workflow.selectedCandidate = rank;
    map.setSelectedRank(rank);
    selectedTitle.setText("SELECTED  " + juce::String(rank).paddedLeft('0', 2), juce::dontSendNotification);
    candidateDetail.setText(candidate->explanation, juce::dontSendNotification);
    if (playImmediately) togglePreview(candidate->file, rank);
    updateTransportUi();
    updateResultVisibility();
    resized();
    saveRunDecisions();
    saveSettings();
}

void DivergeAudioProcessorEditor::recordDecision(CandidateDecision decision)
{
    if (selectedCandidate <= 0) return;
    const auto index = static_cast<size_t>(selectedCandidate - 1);
    workflow.decisions[index] = decision;
    if (auto* candidate = loadedRun.candidate(selectedCandidate)) candidate->decision = decision;
    const auto label = decision == CandidateDecision::pass ? juce::String("discard")
                      : decision == CandidateDecision::favorite ? juce::String("love")
                      : decision == CandidateDecision::exported ? juce::String("export")
                      : juce::String("keep");
    runCriticCommand({ "add", candidateCards[index]->file().getFullPathName(), label,
                       "--events", tasteEventsFile().getFullPathName(), "--model", tasteModelFile().getFullPathName(),
                       "--models-dir", modelsEditor.getText().trim(), "--batch-id", currentRun.getFileName() });
    const auto copy = decision == CandidateDecision::pass ? "Passed - Cmd-Z to undo"
                      : decision == CandidateDecision::favorite ? "Favorited - Diverge will lean toward this"
                      : decision == CandidateDecision::exported ? "Marked used in DAW"
                      : "Kept - Diverge will learn from this";
    showToast(copy);
    updateTransportUi();
    updateResultVisibility();
    resized();
    saveRunDecisions();
    saveSettings();
}

void DivergeAudioProcessorEditor::undoDecision()
{
    if (selectedCandidate <= 0) return;
    const auto index = static_cast<size_t>(selectedCandidate - 1);
    const auto eventId = lastTasteEventIds[index];
    if (eventId.isNotEmpty())
        runCriticCommand({ "undo", eventId, "--events", tasteEventsFile().getFullPathName(),
                           "--model", tasteModelFile().getFullPathName() });
    workflow.decisions[index] = CandidateDecision::none;
    showToast("Decision undone");
    updateTransportUi();
    updateResultVisibility();
    resized();
    saveRunDecisions();
    saveSettings();
}

void DivergeAudioProcessorEditor::dragSelected()
{
    if (selectedCandidate <= 0) return;
    const auto file = candidateCards[static_cast<size_t>(selectedCandidate - 1)]->file();
    if (!file.existsAsFile()) return;
    recordDecision(CandidateDecision::exported);
    juce::DragAndDropContainer::performExternalDragDropOfFiles({ file.getFullPathName() }, false, this);
}

void DivergeAudioProcessorEditor::branchFromSelected()
{
    if (selectedCandidate <= 0) return;
    const auto file = candidateCards[static_cast<size_t>(selectedCandidate - 1)]->file();
    if (!file.existsAsFile()) return;
    audioProcessor.state().setProperty("pendingParentRun", currentRun.getFileName(), nullptr);
    audioProcessor.state().setProperty("pendingParentCandidate", selectedCandidate, nullptr);
    setAudioSlot(0, file);
    setPrepareVisible(true);
    showToast("Variation " + juce::String(selectedCandidate).paddedLeft('0', 2) + " is now your source");
}

void DivergeAudioProcessorEditor::createNew()
{
    audioProcessor.state().removeProperty("pendingParentRun", nullptr);
    audioProcessor.state().removeProperty("pendingParentCandidate", nullptr);
    setPrepareVisible(true);
}

void DivergeAudioProcessorEditor::adjustRange(int delta)
{
    workflow.range = juce::jlimit(0, 100, workflow.range + delta);
    showToast(delta < 0 ? "Next batch will stay closer together" : "Next batch will explore a wider range");
    saveSettings();
}

void DivergeAudioProcessorEditor::saveRunDecisions()
{
    if (!currentRun.isDirectory()) return;
    auto root = juce::JSON::parse("{}");
    auto* object = root.getDynamicObject();
    object->setProperty("run_id", currentRun.getFileName());
    juce::Array<juce::var> rows;
    for (int index = 0; index < 8; ++index)
    {
        const auto decision = workflow.decisions[static_cast<size_t>(index)];
        if (decision == CandidateDecision::none) continue;
        auto row = juce::JSON::parse("{}");
        row.getDynamicObject()->setProperty("rank", index + 1);
        row.getDynamicObject()->setProperty("decision", decisionToString(decision));
        rows.add(row);
    }
    object->setProperty("decisions", rows);
    currentRun.getChildFile("decisions.json").replaceWithText(juce::JSON::toString(root, true));
}

void DivergeAudioProcessorEditor::restoreRunDecisions(bool sameRun)
{
    if (!sameRun) workflow.decisions = {};
    const auto value = juce::JSON::parse(currentRun.getChildFile("decisions.json"));
    if (const auto* object = value.getDynamicObject())
        if (const auto* rows = object->getProperty("decisions").getArray())
            for (const auto& row : *rows)
            {
                const auto rank = static_cast<int>(row.getProperty("rank", 0));
                if (rank >= 1 && rank <= 8)
                    workflow.decisions[static_cast<size_t>(rank - 1)] =
                        decisionFromString(row.getProperty("decision", {}).toString());
            }
}

void DivergeAudioProcessorEditor::refreshRecentRuns()
{
    juce::Array<juce::File> runs;
    juce::File(outputEditor.getText().trim()).findChildFiles(runs, juce::File::findDirectories, false);
    std::sort(runs.begin(), runs.end(), [](const auto& a, const auto& b)
    {
        return a.getLastModificationTime() > b.getLastModificationTime();
    });
    for (int index = 0; index < static_cast<int>(recentCards.size()); ++index)
    {
        auto& card = *recentCards[static_cast<size_t>(index)];
        recentRunDirectories[static_cast<size_t>(index)] = juce::File();
        if (index >= runs.size())
        {
            card.setAudio("Empty", "No earlier run", {});
            card.setSupportingText({});
            continue;
        }
        const auto run = runs.getReference(index);
        const auto model = RunModel::load(run);
        if (!model.isValid())
        {
            card.setAudio("Unavailable", "Run bundle needs repair", {});
            continue;
        }
        recentRunDirectories[static_cast<size_t>(index)] = run;
        int kept = 0;
        const auto decisions = juce::JSON::parse(run.getChildFile("decisions.json"));
        if (const auto* object = decisions.getDynamicObject())
            if (const auto* rows = object->getProperty("decisions").getArray())
                for (const auto& row : *rows)
                {
                    const auto decision = decisionFromString(row.getProperty("decision", {}).toString());
                    if (decision == CandidateDecision::keep || decision == CandidateDecision::favorite
                        || decision == CandidateDecision::exported) ++kept;
                }
        card.setAudio(run == currentRun ? "Current run" : "Saved run", "Run source unavailable", model.source);
        const auto date = run.getLastModificationTime().toString(true, true, false, true);
        card.setSupportingText(date + "  /  " + juce::String(kept) + " kept or used");
    }
}

void DivergeAudioProcessorEditor::positionRecentPanel()
{
    if (recentTargetBounds.isEmpty()) return;
    const auto eased = 1.0f - std::pow(1.0f - recentSlide, 3.0f);
    const auto hiddenX = static_cast<float>(getWidth() + 8);
    const auto x = juce::jmap(eased, hiddenX, static_cast<float>(recentTargetBounds.getX()));
    recentPanel.setTopLeftPosition(juce::roundToInt(x), recentTargetBounds.getY());
}

void DivergeAudioProcessorEditor::closeRecentImmediately()
{
    recentTarget = false;
    recentSlide = 0.0f;
    recentPanel.setVisible(false);
    scrim.setVisible(false);
}

void DivergeAudioProcessorEditor::setRecentVisible(bool visible)
{
    recentTarget = visible;
    if (visible)
    {
        refreshRecentRuns();
        recentPanel.setVisible(true);
        scrim.setVisible(true);
        scrim.setAlpha(recentSlide);
        scrim.toFront(false);
        recentPanel.toFront(false);
        toast.toFront(false);
        if (DivergeTheme::reducedMotion())
        {
            recentSlide = 1.0f;
            scrim.setAlpha(1.0f);
        }
        resized();
        positionRecentPanel();
    }
    else if (DivergeTheme::reducedMotion())
        closeRecentImmediately();
    repaint();
}

void DivergeAudioProcessorEditor::updateResultVisibility()
{
    if (showPrepare || showMap)
    {
        for (auto& card : candidateCards) card->setVisible(false);
        return;
    }
    int visibleCount = 0;
    for (int index = 0; index < 8; ++index)
    {
        const auto decision = workflow.decisions[static_cast<size_t>(index)];
        const auto positive = decision == CandidateDecision::keep || decision == CandidateDecision::favorite
                           || decision == CandidateDecision::exported;
        const auto visible = !keptOnly || positive;
        candidateCards[static_cast<size_t>(index)]->setVisible(visible);
        if (visible) ++visibleCount;
    }
    if (keptOnly && visibleCount == 0)
        candidateDetail.setText("No kept variations in this run yet", juce::dontSendNotification);
}

void DivergeAudioProcessorEditor::showToast(const juce::String& text)
{
    toast.show(text);
}

bool DivergeAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (settingsPanel.isVisible() && key == juce::KeyPress::escapeKey)
    {
        setSettingsVisible(false);
        return true;
    }
    if (key.getModifiers().isCommandDown() && (key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z'))
    {
        undoDecision();
        return true;
    }
    if (showPrepare) return false;
    if (key == juce::KeyPress::spaceKey)
    {
        if (selectedCandidate > 0) togglePreview(candidateCards[static_cast<size_t>(selectedCandidate - 1)]->file(),
                                                  selectedCandidate);
        return true;
    }
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::upKey)
    {
        selectCandidate(juce::jmax(1, selectedCandidate - 1));
        return true;
    }
    if (key == juce::KeyPress::rightKey || key == juce::KeyPress::downKey)
    {
        selectCandidate(juce::jmin(8, selectedCandidate + 1));
        return true;
    }
    const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (character == 'a') { abButton.triggerClick(); return true; }
    if (character == 'k') { recordDecision(CandidateDecision::keep); return true; }
    if (character == 'x') { recordDecision(CandidateDecision::pass); return true; }
    if (character == 'f') { recordDecision(CandidateDecision::favorite); return true; }
    return false;
}

void DivergeAudioProcessorEditor::runCriticCommand(const juce::StringArray& arguments)
{
    if (decisionProcess && decisionProcess->isRunning())
    {
        criticQueue.push_back(arguments);
        return;
    }
    decisionProcess = std::make_unique<juce::ChildProcess>();
    criticAction = arguments[0];
    criticCandidatePath = arguments.size() > 1 ? arguments[1] : juce::String();
    juce::StringArray command { pythonEditor.getText().trim(), "-m", "diverge.cli", "taste" };
    command.addArray(arguments);
    decisionProcess->start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr);
}

juce::File DivergeAudioProcessorEditor::choicesFile() const { return juce::File(choicesEditor.getText().trim()); }

juce::File DivergeAudioProcessorEditor::tasteEventsFile() const
{
    return choicesFile().getParentDirectory().getChildFile("taste/events.jsonl");
}

juce::File DivergeAudioProcessorEditor::tasteModelFile() const
{
    return juce::File(modelsEditor.getText()).getParentDirectory().getChildFile("taste/model.joblib");
}

void DivergeAudioProcessorEditor::trainCritic()
{
    learningStatus.set("Preferences", "Updating from your local choices...", StatusCard::State::neutral);
    runCriticCommand({ "train", "--events", tasteEventsFile().getFullPathName(),
                       "--model", tasteModelFile().getFullPathName() });
}

void DivergeAudioProcessorEditor::pollCriticProcess()
{
    if (!decisionProcess || decisionProcess->isRunning()) return;
    const auto output = decisionProcess->readAllProcessOutput();
    decisionProcess.reset();
    const auto jsonStart = output.indexOfChar('{');
    const auto parsed = juce::JSON::parse(jsonStart >= 0 ? output.substring(jsonStart) : output);
    if (!parsed.isObject())
    {
        learningStatus.set("Preferences", "Local preference update failed - check Advanced diagnostics.",
                           StatusCard::State::attention);
        showToast(friendlyError(output.trim()));
    }
    else if (criticAction == "add")
    {
        if (static_cast<bool>(parsed.getProperty("recorded", true)))
        {
            const auto eventId = parsed.getProperty("event_id", "").toString();
            for (int index = 0; index < 8; ++index)
                if (candidateCards[static_cast<size_t>(index)]->file().getFullPathName() == criticCandidatePath)
                    lastTasteEventIds[static_cast<size_t>(index)] = eventId;
        }
        updateTasteProfile(parsed);
    }
    else if (criticAction == "undo")
    {
        for (auto& eventId : lastTasteEventIds)
            if (eventId == criticCandidatePath) eventId.clear();
        updateTasteProfile(parsed);
    }
    else if (criticAction == "train" || criticAction == "status" || criticAction == "compare")
        updateTasteProfile(parsed);
    else if (criticAction == "reset")
    {
        totalChoiceCount = 0;
        tasteConfidence = 0.0;
        positiveTasteModes = 0;
        negativeTasteModes = 0;
        updateTasteProfile(parsed);
    }
    else if (criticAction == "set-learning")
    {
        workflow.learningEnabled = static_cast<bool>(parsed.getProperty(
            "learning_enabled", workflow.learningEnabled));
        learningToggle.setToggleState(workflow.learningEnabled, juce::dontSendNotification);
        saveSettings();
    }
    else if (criticAction == "import-model")
        updateTasteProfile(parsed);
    if (!criticQueue.empty())
    {
        const auto next = criticQueue.front();
        criticQueue.pop_front();
        runCriticCommand(next);
    }
}

void DivergeAudioProcessorEditor::restoreSettings()
{
    auto& state = audioProcessor.state();
    workflow.restoreFrom(state);
   #if defined(DIVERGE_PROJECT_ROOT)
    const auto project = juce::File(juce::String(DIVERGE_PROJECT_ROOT));
   #else
    const auto project = juce::File::getCurrentWorkingDirectory();
   #endif
    pythonEditor.setText(state.getProperty("python", project.getChildFile(".venv/bin/python").getFullPathName()).toString());
    modelsEditor.setText(state.getProperty("models", project.getChildFile("models").getFullPathName()).toString());
    libraryEditor.setText(state.getProperty("library", "").toString());
    choicesEditor.setText(state.getProperty("choices", project.getChildFile("choices.jsonl").getFullPathName()).toString());
    outputEditor.setText(state.getProperty("output", project.getChildFile("runs").getFullPathName()).toString());
    audioSlots = workflow.audioSlots;
    changeSlider.setValue(workflow.change);
    opinionSlider.setValue(workflow.opinion, juce::dontSendNotification);
    opinionValue.setText(juce::String(workflow.opinion) + "%", juce::dontSendNotification);
    learningToggle.setToggleState(workflow.learningEnabled, juce::dontSendNotification);
    grooveLock.setToggleState(workflow.preserveGroove, juce::dontSendNotification);
    melodyLock.setToggleState(workflow.preserveMelody, juce::dontSendNotification);
    timbreLock.setToggleState(workflow.preserveTimbre, juce::dontSendNotification);
    styleEditor.setText(workflow.direction, false);
    showDirectionText = workflow.direction.isNotEmpty();
    addDirectionButton.setButtonText(showDirectionText ? "- Hide text" : "+ Text direction");
    refreshSlotCard(0);
    refreshSlotCard(1);
    const auto studioReady = juce::File(pythonEditor.getText()).existsAsFile()
                             && juce::File(modelsEditor.getText()).isDirectory();
    studioStatus.set("Studio",
                     studioReady ? "Local engine and models are ready."
                                 : "Setup needs attention - check the paths in Advanced diagnostics.",
                     studioReady ? StatusCard::State::ok : StatusCard::State::attention);
    const auto libraryReady = libraryEditor.getText().trim().isNotEmpty();
    libraryStatus.set("Library",
                      libraryReady ? "Avoid my library is available for future batches."
                                   : "Connect an index to enable library avoidance.",
                      libraryReady ? StatusCard::State::ok : StatusCard::State::neutral);
}

void DivergeAudioProcessorEditor::saveSettings()
{
    auto& state = audioProcessor.state();
    state.setProperty("python", pythonEditor.getText().trim(), nullptr);
    state.setProperty("models", modelsEditor.getText().trim(), nullptr);
    state.setProperty("library", libraryEditor.getText().trim(), nullptr);
    state.setProperty("choices", choicesEditor.getText().trim(), nullptr);
    state.setProperty("output", outputEditor.getText().trim(), nullptr);
    workflow.audioSlots = audioSlots;
    workflow.change = static_cast<int>(changeSlider.getValue());
    workflow.opinion = static_cast<int>(opinionSlider.getValue());
    workflow.learningEnabled = learningToggle.getToggleState();
    workflow.preserveGroove = grooveLock.getToggleState();
    workflow.preserveMelody = melodyLock.getToggleState();
    workflow.preserveTimbre = timbreLock.getToggleState();
    workflow.direction = styleHint();
    workflow.selectedCandidate = selectedCandidate;
    workflow.saveTo(state);
}

void DivergeAudioProcessorEditor::timerCallback()
{
    pollCriticProcess();

    if (snapshotTicks > 0 && --snapshotTicks == 0)
    {
        const auto image = createComponentSnapshot(getLocalBounds());
        snapshotFile.deleteFile();
        juce::FileOutputStream stream(snapshotFile);
        juce::PNGImageFormat png;
        if (stream.openedOk()) png.writeImageToStream(image, stream);
        if (auto* app = juce::JUCEApplicationBase::getInstance()) app->systemRequestedQuit();
    }

    if (transitionAlpha > 0.0f)
    {
        transitionAlpha = juce::jmax(0.0f, transitionAlpha - 0.09f);
        if (transitionAlpha <= 0.0f) transitionImage = {};
        repaint();
    }
    const auto slideTarget = recentTarget ? 1.0f : 0.0f;
    if (recentSlide != slideTarget)
    {
        recentSlide = DivergeTheme::approach(recentSlide, slideTarget, 0.24f);
        positionRecentPanel();
        scrim.setAlpha(recentSlide);
        if (recentSlide <= 0.002f && !recentTarget)
        {
            recentPanel.setVisible(false);
            scrim.setVisible(false);
        }
    }
    if (recentPanel.isVisible())
        for (auto& card : recentCards) card->advanceAnimation();

    if (fixtureMode == "generating" || fixtureMode == "error")
    {
        sourceCard->advanceAnimation();
        directionCard->advanceAnimation();
        for (auto& card : candidateCards) card->advanceAnimation();
        if (fixtureMode == "generating") repaint(progressLabel.getBounds().expanded(8, 8));
        return;
    }
    const auto job = audioProcessor.generation().snapshot();
    const auto targetProgress = job.total > 0 ? static_cast<float>(job.completed) / static_cast<float>(job.total)
                                               : (audioProcessor.generation().isActive() ? 0.04f : displayedProgress);
    displayedProgress += (targetProgress - displayedProgress) * 0.12f;
    if (job.status != lastJobStatus)
    {
        lastJobStatus = job.status;
        if (job.status == JobRunner::Status::complete && job.run != currentRun)
        {
            displayedProgress = 1.0f;
            loadRun(job.run);
        }
        else if (job.status == JobRunner::Status::failed)
        {
            workflow.view = WorkflowViewState::recoverableError;
            progressLabel.setText(friendlyError(job.error), juce::dontSendNotification);
            progressLabel.setColour(juce::Label::textColourId, DivergeTheme::danger);
            showToast(friendlyError(job.error));
        }
        else if (job.status == JobRunner::Status::cancelled)
        {
            workflow.view = audioSlots[0].existsAsFile() ? WorkflowViewState::ready : WorkflowViewState::needsSetup;
            progressLabel.setText(job.message, juce::dontSendNotification);
        }
    }
    if (audioProcessor.generation().isActive())
        progressLabel.setText(job.message + (job.total > 0 ? "  /  " + juce::String(job.completed) + "/" + juce::String(job.total)
                                                  : juce::String()), juce::dontSendNotification);
    const auto active = audioProcessor.generation().isActive();
    generateButton.setEnabled(!active);
    generateButton.setButtonText(active ? "Creating..." : "Create 8 variations");
    cancelButton.setVisible(showPrepare && active);

    if (!audioProcessor.isPreviewPlaying())
    {
        playingCandidate = 0;
        playingSource = false;
    }
    updateTransportUi();
    sourceCard->advanceAnimation();
    directionCard->advanceAnimation();
    if (!audioProcessor.isCapturing() && recordButton.getButtonText() == "Stop") toggleCapture();
    if (active && progressLabel.isVisible())
        repaint(progressLabel.getBounds().expanded(8, 8));
}
