#include "PluginEditor.h"
#include <algorithm>

namespace
{
void configureSectionLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(11.5f).withStyle("Bold"));
    label.setColour(juce::Label::textColourId, DivergeTheme::muted);
}

void configureSupportingLabel(juce::Label& label, const juce::String& text,
                              juce::Justification justification = juce::Justification::centredLeft)
{
    label.setText(text, juce::dontSendNotification);
    label.setFont(juce::FontOptions(12.5f));
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
    g.setColour(DivergeTheme::surface);
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
    g.setColour(DivergeTheme::edge);
    g.drawRoundedRectangle(bounds, DivergeTheme::radius, 1.0f);
    if (points.empty())
    {
        g.setColour(DivergeTheme::muted);
        g.setFont(juce::FontOptions(15.0f));
        g.drawFittedText("The variation map appears after a batch is ready.", getLocalBounds().reduced(48),
                         juce::Justification::centred, 2);
        return;
    }

    juce::Path traces;
    juce::Point<float> sourcePoint;
    for (const auto& point : points)
        if (point.kind == "source") sourcePoint = positionFor(point);
    for (const auto& point : points)
        if (point.rank > 0)
        {
            traces.startNewSubPath(sourcePoint);
            traces.lineTo(positionFor(point));
        }
    g.setColour(DivergeTheme::edge.withAlpha(0.7f));
    g.strokePath(traces, juce::PathStrokeType(1.0f));

    for (const auto& point : points)
    {
        const auto position = positionFor(point);
        if (point.kind == "source")
        {
            g.setColour(DivergeTheme::text);
            g.fillEllipse(juce::Rectangle<float>(15.0f, 15.0f).withCentre(position));
        }
        else if (point.kind == "reference")
        {
            g.setColour(DivergeTheme::decision);
            g.fillRoundedRectangle(juce::Rectangle<float>(14.0f, 14.0f).withCentre(position), 3.0f);
        }
        else
        {
            auto colour = point.rank == selectedRank ? DivergeTheme::exploration : DivergeTheme::muted;
            if (point.rank >= 1 && point.rank <= 8)
            {
                const auto decision = decisions[static_cast<size_t>(point.rank - 1)];
                if (decision == CandidateDecision::keep || decision == CandidateDecision::favorite
                    || decision == CandidateDecision::exported)
                    colour = DivergeTheme::decision;
            }
            const auto size = point.rank == selectedRank ? 30.0f : 24.0f;
            g.setColour(colour);
            g.fillEllipse(juce::Rectangle<float>(size, size).withCentre(position));
            g.setColour(DivergeTheme::canvas);
            g.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
            g.drawText(juce::String(point.rank), juce::Rectangle<float>(size, size).withCentre(position),
                       juce::Justification::centred);
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

    configureUi();
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
    trainCritic();
    startTimerHz(30);
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
        sourceCard->setAudio("Source", "Drop, record, or choose audio", {});
        directionCard->setAudio("Direction", "Add an optional reference", {});
        setPrepareVisible(true);
        return true;
    }
    const auto fixture = WorkflowFixtures::make(
        fixtureMode == "results" ? WorkflowViewState::results
        : fixtureMode == "generating" ? WorkflowViewState::generating
        : fixtureMode == "error" ? WorkflowViewState::recoverableError
        : WorkflowViewState::ready);
    workflow.change = fixture.change;
    changeSlider.setValue(workflow.change);
    audioSlots[0] = project.getChildFile("data/loop_a.wav");
    audioSlots[1] = project.getChildFile("data/ref_a.wav");
    sourceCard->setAudio("Source", "Drop, record, or choose audio", audioSlots[0]);
    directionCard->setAudio("Direction", "Add an optional reference", audioSlots[1]);
    setPrepareVisible(true);
    if (fixtureMode == "generating")
    {
        generateButton.setEnabled(false);
        generateButton.setButtonText("Creating...");
        progressLabel.setText("Creating candidates  /  7/16", juce::dontSendNotification);
        displayedProgress = 7.0f / 16.0f;
    }
    else if (fixtureMode == "error")
        progressLabel.setText("Local engine unavailable - open Settings for a quick health check",
                              juce::dontSendNotification);
    else if (fixtureMode == "results")
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
    }
    return true;
}

DivergeAudioProcessorEditor::~DivergeAudioProcessorEditor()
{
    saveSettings();
    audioProcessor.stopPreview();
    if (decisionProcess && decisionProcess->isRunning()) decisionProcess->kill();
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
             &addDirectionButton, &styleEditor, &changeSection, &changeSlider, &familiarLabel, &wildLabel,
             &preserveSection, &grooveLock, &melodyLock, &timbreLock, &generateButton, &cancelButton,
             &progressLabel, &privacyLabel, &briefButton, &resultsTitle, &gridButton, &mapButton, &newButton,
             &map, &selectedTitle, &candidateDetail, &abButton, &passButton, &keepButton, &favoriteButton,
             &branchButton, &dragButton, &tighterButton, &widerButton, &shortcutLabel, &toastLabel,
             &settingsPanel })
        addAndMakeVisible(component);
    for (auto& card : candidateCards) addAndMakeVisible(card.get());

    brandLabel.setText("DIVERGE", juce::dontSendNotification);
    brandLabel.setFont(juce::FontOptions(23.0f).withStyle("Bold"));
    promiseLabel.setText("Recognizable where you choose. Different where it matters.", juce::dontSendNotification);
    promiseLabel.setFont(juce::FontOptions(12.5f));
    promiseLabel.setColour(juce::Label::textColourId, DivergeTheme::muted);
    configureSupportingLabel(localBadge, "LOCAL", juce::Justification::centred);
    localBadge.setColour(juce::Label::textColourId, DivergeTheme::exploration);
    settingsButton.setButtonText("...");

    configureSectionLabel(sourceSection, "SOURCE");
    configureSectionLabel(directionSection, "DIRECTION  /  OPTIONAL");
    configureSectionLabel(changeSection, "CHANGE");
    configureSectionLabel(preserveSection, "PRESERVE");
    sourceCard->setAudio("Source", "Drop, record, or choose audio", {});
    directionCard->setAudio("Direction", "Add an optional reference", {});
    sourceCard->onChoose = [this] { chooseAudio(0); };
    sourceCard->onActivate = [this] { togglePreview(audioSlots[0], 0, true); };
    directionCard->onChoose = [this] { chooseAudio(1); };
    directionCard->onActivate = [this] { togglePreview(audioSlots[1]); };
    recordButton.onClick = [this] { toggleCapture(); };
    addDirectionButton.onClick = [this]
    {
        showDirectionText = !showDirectionText;
        styleEditor.setVisible(showDirectionText && showPrepare);
        addDirectionButton.setButtonText(showDirectionText ? "- Hide direction" : "+ Add direction");
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
    configureSupportingLabel(wildLabel, "UNRECOGNIZABLE", juce::Justification::centredRight);
    grooveLock.setToggleState(true, juce::dontSendNotification);
    for (auto* lock : { &grooveLock, &melodyLock, &timbreLock }) lock->setClickingTogglesState(true);

    generateButton.setColour(juce::TextButton::buttonColourId, DivergeTheme::exploration);
    generateButton.setColour(juce::TextButton::textColourOffId, DivergeTheme::canvas);
    generateButton.onClick = [this] { startGeneration(); };
    cancelButton.onClick = [this] { audioProcessor.generation().cancel(); };
    cancelButton.setVisible(false);
    configureSupportingLabel(progressLabel, "Ready when you are", juce::Justification::centred);
    configureSupportingLabel(privacyLabel, "Audio stays on this Mac.", juce::Justification::centred);

    briefButton.onClick = [this] { setPrepareVisible(true); };
    resultsTitle.setText("8 VARIATIONS", juce::dontSendNotification);
    resultsTitle.setFont(juce::FontOptions(16.0f).withStyle("Bold"));
    gridButton.setClickingTogglesState(true);
    mapButton.setClickingTogglesState(true);
    gridButton.setToggleState(true, juce::dontSendNotification);
    gridButton.onClick = [this]
    {
        showMap = false;
        gridButton.setToggleState(true, juce::dontSendNotification);
        mapButton.setToggleState(false, juce::dontSendNotification);
        map.setVisible(false);
        for (auto& card : candidateCards) card->setVisible(true);
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
    map.onCandidateSelected = [this](int rank) { selectCandidate(rank); };

    for (int index = 0; index < 8; ++index)
    {
        auto& card = *candidateCards[static_cast<size_t>(index)];
        card.setAudio(juce::String(index + 1).paddedLeft('0', 2), "Waiting for audio", {});
        card.setDraggable(true);
        card.onActivate = [this, index] { selectCandidate(index + 1); };
        card.onDrag = [this, index]
        {
            selectCandidate(index + 1, false);
            dragSelected();
        };
    }

    selectedTitle.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    configureSupportingLabel(candidateDetail, "Choose a variation to hear it");
    abButton.onClick = [this]
    {
        if (selectedCandidate <= 0) return;
        if (playingSource) togglePreview(candidateCards[static_cast<size_t>(selectedCandidate - 1)]->file(), selectedCandidate);
        else togglePreview(audioSlots[0], 0, true);
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
    configureSupportingLabel(shortcutLabel, "SPACE play  /  arrows choose  /  A source  /  K keep  /  X pass  /  Cmd-Z undo",
                             juce::Justification::centredRight);
    toastLabel.setJustificationType(juce::Justification::centred);
    toastLabel.setFont(juce::FontOptions(12.5f).withStyle("Bold"));
    toastLabel.setColour(juce::Label::backgroundColourId, DivergeTheme::raised);
    toastLabel.setColour(juce::Label::textColourId, DivergeTheme::text);
    toastLabel.setVisible(false);

    settingsButton.onClick = [this] { setSettingsVisible(true); };
    settingsPanel.setVisible(false);
    for (auto* component : std::initializer_list<juce::Component*> {
             &settingsTitle, &settingsClose, &studioStatus, &learningStatus, &libraryStatus, &advancedButton,
             &pythonLabel, &pythonEditor, &modelsLabel, &modelsEditor, &libraryLabel, &libraryEditor,
             &choicesLabel, &choicesEditor, &outputLabel, &outputEditor })
        settingsPanel.addAndMakeVisible(component);
    settingsTitle.setText("SETTINGS", juce::dontSendNotification);
    settingsTitle.setFont(juce::FontOptions(20.0f).withStyle("Bold"));
    settingsClose.onClick = [this] { saveSettings(); setSettingsVisible(false); };
    configureSupportingLabel(studioStatus, "STUDIO\nLocal engine and models are checked before creation.");
    configureSupportingLabel(learningStatus, "PREFERENCES\nLearns only from choices you make. Stored locally.");
    configureSupportingLabel(libraryStatus, "LIBRARY\nLibrary avoidance appears only after an index is connected.");
    for (auto* status : { &studioStatus, &learningStatus, &libraryStatus })
        status->setFont(juce::FontOptions(13.0f));
    advancedButton.onClick = [this] { setAdvancedVisible(!showAdvanced); };
    pythonLabel.setText("Engine", juce::dontSendNotification);
    modelsLabel.setText("Models", juce::dontSendNotification);
    libraryLabel.setText("Library index", juce::dontSendNotification);
    choicesLabel.setText("Choices", juce::dontSendNotification);
    outputLabel.setText("Storage", juce::dontSendNotification);
    for (auto* editor : { &pythonEditor, &modelsEditor, &libraryEditor, &choicesEditor, &outputEditor })
        editor->onFocusLost = [this] { saveSettings(); };
    setAdvancedVisible(false);
}

void DivergeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(DivergeTheme::canvas);
    g.setColour(DivergeTheme::edge.withAlpha(0.32f));
    for (int y = 72; y < getHeight(); y += 64)
        g.drawHorizontalLine(y, 24.0f, static_cast<float>(getWidth() - 24));
    g.setGradientFill(juce::ColourGradient(DivergeTheme::exploration.withAlpha(0.07f), 0.0f, 0.0f,
                                            juce::Colours::transparentBlack, static_cast<float>(getWidth()) * 0.7f,
                                            static_cast<float>(getHeight()) * 0.7f, false));
    g.fillRect(getLocalBounds());
    if (dragHover)
    {
        g.setColour(DivergeTheme::exploration.withAlpha(0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 14.0f);
        g.setColour(DivergeTheme::exploration);
        g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 14.0f, 2.0f);
    }
    if (settingsPanel.isVisible())
    {
        g.setColour(DivergeTheme::canvas.withAlpha(0.92f));
        g.fillRect(settingsPanel.getBounds());
    }
    if (audioProcessor.generation().isActive() && progressLabel.isVisible())
    {
        auto bar = progressLabel.getBounds().toFloat().removeFromBottom(3.0f).reduced(20.0f, 0.0f);
        g.setColour(DivergeTheme::edge); g.fillRoundedRectangle(bar, 1.5f);
        g.setColour(DivergeTheme::exploration);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * displayedProgress), 1.5f);
    }
}

void DivergeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto header = area.removeFromTop(40);
    brandLabel.setBounds(header.removeFromLeft(128));
    promiseLabel.setBounds(header.removeFromLeft(470));
    settingsButton.setBounds(header.removeFromRight(44));
    header.removeFromRight(8);
    localBadge.setBounds(header.removeFromRight(82));
    area.removeFromTop(14);

    if (settingsPanel.isVisible())
    {
        settingsPanel.setBounds(area);
        auto settings = settingsPanel.getLocalBounds().reduced(28);
        auto titleRow = settings.removeFromTop(44);
        settingsTitle.setBounds(titleRow.removeFromLeft(240));
        settingsClose.setBounds(titleRow.removeFromRight(88));
        settings.removeFromTop(18);
        auto cards = settings.removeFromTop(102);
        const auto statusWidth = (cards.getWidth() - 24) / 3;
        studioStatus.setBounds(cards.removeFromLeft(statusWidth)); cards.removeFromLeft(12);
        learningStatus.setBounds(cards.removeFromLeft(statusWidth)); cards.removeFromLeft(12);
        libraryStatus.setBounds(cards);
        settings.removeFromTop(18);
        advancedButton.setBounds(settings.removeFromTop(42).removeFromLeft(190));
        settings.removeFromTop(12);
        auto place = [&settings](juce::Label& label, juce::TextEditor& editor)
        {
            auto row = settings.removeFromTop(42);
            label.setBounds(row.removeFromLeft(110));
            editor.setBounds(row);
            settings.removeFromTop(6);
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
        addDirectionButton.setBounds(directionHeader.removeFromRight(142));
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
        mapButton.setBounds(toolbar.removeFromRight(64)); toolbar.removeFromRight(6);
        gridButton.setBounds(toolbar.removeFromRight(64));
        area.removeFromTop(10);

        auto selected = area.removeFromBottom(154);
        area.removeFromBottom(10);
        map.setBounds(area);
        if (!showMap)
        {
            const auto columns = getWidth() >= 1260 ? 4 : 2;
            const auto rows = 8 / columns;
            const auto gap = 8;
            const auto cardWidth = (area.getWidth() - gap * (columns - 1)) / columns;
            const auto cardHeight = (area.getHeight() - gap * (rows - 1)) / rows;
            for (int index = 0; index < 8; ++index)
            {
                const auto column = index % columns;
                const auto row = index / columns;
                candidateCards[static_cast<size_t>(index)]->setBounds(
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
    }
    toastLabel.setBounds(getLocalBounds().withSizeKeepingCentre(420, 38).translated(0, getHeight() / 2 - 42));
    toastLabel.toFront(false);
}

void DivergeAudioProcessorEditor::setPrepareVisible(bool visible)
{
    showPrepare = visible;
    const auto generating = audioProcessor.generation().isActive();
    for (auto* component : std::initializer_list<juce::Component*> {
             &sourceSection, sourceCard.get(), &recordButton, &directionSection, directionCard.get(),
             &addDirectionButton, &changeSection, &changeSlider, &familiarLabel, &wildLabel, &preserveSection,
             &grooveLock, &melodyLock, &timbreLock, &generateButton, &progressLabel, &privacyLabel })
        component->setVisible(visible);
    styleEditor.setVisible(visible && showDirectionText);
    cancelButton.setVisible(visible && generating);
    for (auto* component : std::initializer_list<juce::Component*> {
             &briefButton, &resultsTitle, &gridButton, &mapButton, &newButton, &map, &selectedTitle,
             &candidateDetail, &abButton, &passButton, &keepButton, &favoriteButton, &branchButton,
             &dragButton, &tighterButton, &widerButton, &shortcutLabel })
        component->setVisible(!visible);
    for (auto& card : candidateCards) card->setVisible(!visible && !showMap);
    map.setVisible(!visible && showMap);
    resized();
    repaint();
}

void DivergeAudioProcessorEditor::setSettingsVisible(bool visible)
{
    settingsPanel.setVisible(visible);
    if (visible) settingsPanel.toFront(false);
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

void DivergeAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    if (files.size() == 1) setAudioSlot(0, juce::File(files[0]));
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

void DivergeAudioProcessorEditor::setAudioSlot(int slot, const juce::File& file)
{
    audioSlots[static_cast<size_t>(slot)] = file;
    workflow.audioSlots[static_cast<size_t>(slot)] = file;
    if (slot == 0)
        sourceCard->setAudio("Source", "Drop, record, or choose audio", file);
    else if (slot == 1)
        directionCard->setAudio("Direction", "Add an optional reference", file);
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
    else if (audioProcessor.loadPreview(file))
    {
        audioProcessor.playPreview();
        playingCandidate = candidateRank;
        playingSource = source;
    }
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
    object->setProperty("duration_s", 8.0);
    object->setProperty("seed", juce::Random::getSystemRandom().nextInt());
    object->setProperty("library_index", libraryEditor.getText().trim());
    object->setProperty("critic_model", juce::File(modelsEditor.getText()).getChildFile("critic.joblib").getFullPathName());
    object->setProperty("choices_path", choicesFile().getFullPathName());
    object->setProperty("taste_events_path", tasteEventsFile().getFullPathName());
    object->setProperty("taste_model_path", tasteModelFile().getFullPathName());
    object->setProperty("opinion", 50);
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
    progressLabel.setText("Preparing your source", juce::dontSendNotification);
    displayedProgress = 0.02f;
    repaint();
}

void DivergeAudioProcessorEditor::loadRun(const juce::File& run)
{
    loadedRun = RunModel::load(run);
    if (!loadedRun.isValid())
    {
        showToast("The completed run could not be loaded - its bundle is still safe");
        return;
    }
    currentRun = run;
    map.setPoints(loadedRun.mapPoints);
    for (int index = 0; index < 8; ++index)
    {
        auto& card = *candidateCards[static_cast<size_t>(index)];
        if (const auto* candidate = loadedRun.candidate(index + 1))
        {
            card.setAudio(juce::String(index + 1).paddedLeft('0', 2), "Missing audio", candidate->file);
            card.setSupportingText(candidate->explanation);
            card.setDraggable(true);
        }
    }
    workflow.activeRunId = run.getFileName();
    workflow.view = WorkflowViewState::results;
    const auto restore = workflow.selectedCandidate >= 1 ? workflow.selectedCandidate : 1;
    setPrepareVisible(false);
    selectCandidate(restore, false);
    if (isShowing()) candidateCards[static_cast<size_t>(restore - 1)]->grabKeyboardFocus();
    showToast("Eight variations ready - click one to hear it");
    saveSettings();
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

void DivergeAudioProcessorEditor::showToast(const juce::String& text)
{
    toastLabel.setText(text, juce::dontSendNotification);
    toastLabel.setAlpha(1.0f);
    toastLabel.setVisible(true);
    toastLabel.toFront(false);
    toastTicks = 90;
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
    learningStatus.setText("PREFERENCES\nUpdating from your local choices...", juce::dontSendNotification);
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
    if (criticAction == "add")
    {
        totalChoiceCount = static_cast<int>(parsed.getProperty("observations", totalChoiceCount));
        const auto eventId = parsed.getProperty("event_id", "").toString();
        for (int index = 0; index < 8; ++index)
            if (candidateCards[static_cast<size_t>(index)]->file().getFullPathName() == criticCandidatePath)
                lastTasteEventIds[static_cast<size_t>(index)] = eventId;
    }
    else if (criticAction == "undo")
    {
        for (auto& eventId : lastTasteEventIds)
            if (eventId == criticCandidatePath) eventId.clear();
    }
    else if (criticAction == "train")
        totalChoiceCount = static_cast<int>(parsed.getProperty("observations", 0));
    learningStatus.setText(totalChoiceCount > 0
        ? "PREFERENCES\nLearning locally from the choices you make."
        : "PREFERENCES\nReady to learn from your Keeps and Passes.", juce::dontSendNotification);
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
    grooveLock.setToggleState(workflow.preserveGroove, juce::dontSendNotification);
    melodyLock.setToggleState(workflow.preserveMelody, juce::dontSendNotification);
    timbreLock.setToggleState(workflow.preserveTimbre, juce::dontSendNotification);
    styleEditor.setText(workflow.direction, false);
    showDirectionText = workflow.direction.isNotEmpty();
    addDirectionButton.setButtonText(showDirectionText ? "- Hide direction" : "+ Add direction");
    sourceCard->setAudio("Source", "Drop, record, or choose audio", audioSlots[0]);
    directionCard->setAudio("Direction", "Add an optional reference", audioSlots[1]);
    studioStatus.setText(juce::File(pythonEditor.getText()).existsAsFile() && juce::File(modelsEditor.getText()).isDirectory()
        ? "STUDIO\nLocal engine and models are ready."
        : "STUDIO\nSetup needs attention in Advanced diagnostics.", juce::dontSendNotification);
    libraryStatus.setText(libraryEditor.getText().trim().isNotEmpty()
        ? "LIBRARY\nAvoid my library is available for future batches."
        : "LIBRARY\nConnect an index to enable library avoidance.", juce::dontSendNotification);
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
    if (fixtureMode == "generating" || fixtureMode == "error")
    {
        sourceCard->advanceAnimation();
        directionCard->advanceAnimation();
        for (auto& card : candidateCards) card->advanceAnimation();
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
    if (toastTicks > 0)
    {
        --toastTicks;
        if (toastTicks < 15) toastLabel.setAlpha(static_cast<float>(toastTicks) / 15.0f);
        if (toastTicks == 0) toastLabel.setVisible(false);
    }
    repaint();
}
