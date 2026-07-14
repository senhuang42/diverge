#include "PluginEditor.h"

namespace
{
constexpr auto background = 0xff0b1020;
constexpr auto panel = 0xff151d32;
constexpr auto accent = 0xff38bdf8;

void configureKnob(juce::Slider& slider, double initial)
{
    slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 20);
    slider.setRange(0.0, 100.0, 1.0);
    slider.setValue(initial);
}

juce::String quotedPath(const juce::File& file)
{
    return file.getFullPathName();
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

juce::Point<float> MapComponent::positionFor(const MapPoint& point) const
{
    if (points.empty())
        return getLocalBounds().toFloat().getCentre();
    auto minX = points.front().x, maxX = points.front().x;
    auto minY = points.front().y, maxY = points.front().y;
    for (const auto& item : points)
    {
        minX = juce::jmin(minX, item.x); maxX = juce::jmax(maxX, item.x);
        minY = juce::jmin(minY, item.y); maxY = juce::jmax(maxY, item.y);
    }
    const auto bounds = getLocalBounds().toFloat().reduced(24.0f);
    const auto nx = (point.x - minX) / juce::jmax(0.0001f, maxX - minX);
    const auto ny = (point.y - minY) / juce::jmax(0.0001f, maxY - minY);
    return { bounds.getX() + nx * bounds.getWidth(), bounds.getBottom() - ny * bounds.getHeight() };
}

void MapComponent::paint(juce::Graphics& g)
{
    g.setColour(juce::Colour(panel));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 12.0f);
    if (points.empty())
    {
        g.setColour(juce::Colour(0xff64748b));
        g.setFont(juce::FontOptions(18.0f));
        g.drawFittedText("Generated candidates will appear here.\nClick a map point or number to select it.",
                         getLocalBounds().reduced(40), juce::Justification::centred, 2);
        return;
    }
    for (const auto& point : points)
    {
        const auto position = positionFor(point);
        if (point.kind == "source")
        {
            g.setColour(juce::Colours::white);
            g.fillEllipse(juce::Rectangle<float>(14.0f, 14.0f).withCentre(position));
        }
        else if (point.kind == "reference")
        {
            g.setColour(juce::Colour(0xffa78bfa));
            g.fillRect(juce::Rectangle<float>(14.0f, 14.0f).withCentre(position));
        }
        else
        {
            g.setColour(point.rank == selectedRank ? juce::Colours::orange : juce::Colour(accent));
            g.fillEllipse(juce::Rectangle<float>(24.0f, 24.0f).withCentre(position));
            g.setColour(juce::Colour(background));
            g.drawText(juce::String(point.rank), juce::Rectangle<float>(24.0f, 24.0f).withCentre(position),
                       juce::Justification::centred);
        }
    }
}

void MapComponent::mouseDown(const juce::MouseEvent& event)
{
    float bestDistance = 28.0f;
    int bestRank = 0;
    for (const auto& point : points)
        if (point.rank > 0)
        {
            const auto distance = positionFor(point).getDistanceFrom(event.position);
            if (distance < bestDistance) { bestDistance = distance; bestRank = point.rank; }
        }
    if (bestRank > 0 && onCandidateSelected)
        onCandidateSelected(bestRank);
}

DivergeAudioProcessorEditor::DivergeAudioProcessorEditor(DivergeAudioProcessor& owner)
    : AudioProcessorEditor(&owner), audioProcessor(owner)
{
    setSize(1040, 760);
    setResizable(true, true);
    setResizeLimits(820, 620, 1600, 1100);

    titleLabel.setText("DIVERGE", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(28.0f).withStyle("Bold"));
    tasteLabel.setText("taste model: collecting choices", juce::dontSendNotification);
    for (auto* component : std::initializer_list<juce::Component*> {
             &titleLabel, &tasteLabel, &sourceButton, &recordButton, &grooveLock, &melodyLock,
             &timbreLock, &transformSlider, &spreadSlider, &driftSlider, &styleEditor,
             &opinionSlider, &transformLabel, &spreadLabel, &driftLabel, &opinionLabel,
             &fastMode, &generateButton,
             &cancelButton, &progressLabel, &map, &auditionButton, &keepButton,
             &loveButton, &discardButton, &undoButton, &dragButton, &candidateDetail,
             &settingsButton, &settingsPanel })
        addAndMakeVisible(component);
    for (auto& button : referenceButtons) addAndMakeVisible(button);
    for (auto& slider : referenceWeights) addAndMakeVisible(slider);
    for (auto& button : candidateButtons) addAndMakeVisible(button);

    sourceButton.onClick = [this] { chooseAudio(0); };
    recordButton.onClick = [this] { toggleCapture(); };
    referenceButtons[0].onClick = [this] { chooseAudio(1); };
    referenceButtons[1].onClick = [this] { chooseAudio(2); };
    for (auto& slider : referenceWeights)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 20);
        slider.setRange(0.0, 1.0, 0.01);
        slider.setValue(0.5);
    }
    grooveLock.setToggleState(true, juce::dontSendNotification);
    configureKnob(transformSlider, 45);
    configureKnob(spreadSlider, 60);
    configureKnob(driftSlider, 35);
    configureKnob(opinionSlider, 50);
    transformLabel.setText("TRANSFORM", juce::dontSendNotification);
    spreadLabel.setText("SPREAD", juce::dontSendNotification);
    driftLabel.setText("DRIFT", juce::dontSendNotification);
    opinionLabel.setText("OPINION", juce::dontSendNotification);
    for (auto* label : { &transformLabel, &spreadLabel, &driftLabel, &opinionLabel })
        label->setJustificationType(juce::Justification::centred);
    fastMode.setToggleState(true, juce::dontSendNotification);
    styleEditor.setTextToShowWhenEmpty("Optional style hint (e.g. dry analog drum loop)", juce::Colours::grey);
    generateButton.onClick = [this] { startGeneration(); };
    cancelButton.onClick = [this] { job.cancel(); progressLabel.setText("Cancelled", juce::dontSendNotification); };
    cancelButton.setEnabled(false);

    map.onCandidateSelected = [this](int rank) { selectCandidate(rank); };
    for (int index = 0; index < static_cast<int>(candidateButtons.size()); ++index)
    {
        candidateButtons[static_cast<size_t>(index)].setButtonText(juce::String(index + 1));
        candidateButtons[static_cast<size_t>(index)].setEnabled(false);
        candidateButtons[static_cast<size_t>(index)].onClick = [this, index] { selectCandidate(index + 1); };
    }
    auditionButton.onClick = [this]
    {
        if (selectedCandidate > 0 && audioProcessor.loadPreview(candidates[static_cast<size_t>(selectedCandidate - 1)]))
            audioProcessor.playPreview();
    };
    loveButton.onClick = [this] { recordDecision("love"); };
    keepButton.onClick = [this] { recordDecision("keep"); };
    discardButton.onClick = [this] { recordDecision("discard"); };
    undoButton.onClick = [this]
    {
        if (selectedCandidate <= 0) return;
        const auto eventId = lastTasteEventIds[static_cast<size_t>(selectedCandidate - 1)];
        if (eventId.isEmpty())
        {
            candidateDetail.setText("Nothing to undo for this candidate", juce::dontSendNotification);
            return;
        }
        runCriticCommand({ "undo", eventId, "--events", tasteEventsFile().getFullPathName(),
                           "--model", tasteModelFile().getFullPathName() });
    };
    dragButton.onClick = [this]
    {
        if (selectedCandidate > 0)
            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                { candidates[static_cast<size_t>(selectedCandidate - 1)].getFullPathName() }, false, this);
    };
    auditionButton.setEnabled(false); loveButton.setEnabled(false); keepButton.setEnabled(false);
    discardButton.setEnabled(false); undoButton.setEnabled(false); dragButton.setEnabled(false);

    settingsButton.onClick = [this]
    {
        settingsPanel.setVisible(!settingsPanel.isVisible());
        resized();
    };
    settingsPanel.setVisible(false);
    for (auto* component : std::initializer_list<juce::Component*> {
             &pythonLabel, &pythonEditor, &modelsLabel, &modelsEditor,
             &libraryLabel, &libraryEditor, &choicesLabel, &choicesEditor,
             &outputLabel, &outputEditor })
        settingsPanel.addAndMakeVisible(component);
    pythonLabel.setText("Python", juce::dontSendNotification);
    modelsLabel.setText("Models", juce::dontSendNotification);
    libraryLabel.setText("Library index", juce::dontSendNotification);
    choicesLabel.setText("Choices", juce::dontSendNotification);
    outputLabel.setText("Output", juce::dontSendNotification);
    for (auto* editor : { &pythonEditor, &modelsEditor, &libraryEditor, &choicesEditor, &outputEditor })
        editor->onFocusLost = [this] { saveSettings(); };
    restoreSettings();
    trainCritic();
    startTimerHz(5);
}

DivergeAudioProcessorEditor::~DivergeAudioProcessorEditor()
{
    saveSettings();
    job.cancel();
    if (decisionProcess && decisionProcess->isRunning()) decisionProcess->kill();
}

void DivergeAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(background));
    g.setColour(juce::Colour(0xffe2e8f0));
}

void DivergeAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto header = area.removeFromTop(42);
    titleLabel.setBounds(header.removeFromLeft(180));
    settingsButton.setBounds(header.removeFromRight(100));
    tasteLabel.setBounds(header);

    if (settingsPanel.isVisible())
    {
        auto settings = area.removeFromTop(172);
        settingsPanel.setBounds(settings);
        auto row = settingsPanel.getLocalBounds().reduced(8);
        auto place = [&row](juce::Label& label, juce::TextEditor& editor)
        {
            auto line = row.removeFromTop(30); label.setBounds(line.removeFromLeft(100)); editor.setBounds(line);
        };
        place(pythonLabel, pythonEditor); place(modelsLabel, modelsEditor);
        place(libraryLabel, libraryEditor); place(choicesLabel, choicesEditor);
        place(outputLabel, outputEditor);
        area.removeFromTop(8);
    }

    auto left = area.removeFromLeft(390);
    area.removeFromLeft(14);
    auto sourceRow = left.removeFromTop(38);
    sourceButton.setBounds(sourceRow.removeFromLeft(270));
    sourceRow.removeFromLeft(8); recordButton.setBounds(sourceRow);
    left.removeFromTop(8);
    for (int index = 0; index < 2; ++index)
    {
        auto row = left.removeFromTop(36);
        referenceButtons[static_cast<size_t>(index)].setBounds(row.removeFromLeft(245));
        row.removeFromLeft(8); referenceWeights[static_cast<size_t>(index)].setBounds(row);
    }
    auto locks = left.removeFromTop(34);
    grooveLock.setBounds(locks.removeFromLeft(90)); melodyLock.setBounds(locks.removeFromLeft(90));
    timbreLock.setBounds(locks.removeFromLeft(90)); fastMode.setBounds(locks);
    auto knobs = left.removeFromTop(120);
    const auto knobWidth = knobs.getWidth() / 4;
    auto transformArea = knobs.removeFromLeft(knobWidth);
    transformLabel.setBounds(transformArea.removeFromTop(20)); transformSlider.setBounds(transformArea);
    auto spreadArea = knobs.removeFromLeft(knobWidth);
    spreadLabel.setBounds(spreadArea.removeFromTop(20)); spreadSlider.setBounds(spreadArea);
    auto driftArea = knobs.removeFromLeft(knobWidth);
    driftLabel.setBounds(driftArea.removeFromTop(20)); driftSlider.setBounds(driftArea);
    opinionLabel.setBounds(knobs.removeFromTop(20)); opinionSlider.setBounds(knobs);
    styleEditor.setBounds(left.removeFromTop(58));
    left.removeFromTop(8);
    auto actions = left.removeFromTop(38);
    generateButton.setBounds(actions.removeFromLeft(190)); actions.removeFromLeft(8); cancelButton.setBounds(actions);
    progressLabel.setBounds(left.removeFromTop(32));

    auto detail = area.removeFromBottom(164);
    map.setBounds(area);
    auto numbers = detail.removeFromTop(38);
    for (auto& button : candidateButtons)
        button.setBounds(numbers.removeFromLeft(48).reduced(3));
    candidateDetail.setBounds(detail.removeFromTop(42));
    auto decision = detail.removeFromTop(42);
    auditionButton.setBounds(decision.removeFromLeft(100)); decision.removeFromLeft(6);
    loveButton.setBounds(decision.removeFromLeft(64)); decision.removeFromLeft(5);
    keepButton.setBounds(decision.removeFromLeft(64)); decision.removeFromLeft(5);
    discardButton.setBounds(decision.removeFromLeft(74)); decision.removeFromLeft(5);
    undoButton.setBounds(decision.removeFromLeft(64)); decision.removeFromLeft(5);
    dragButton.setBounds(decision.removeFromLeft(130));
}

bool DivergeAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && juce::File(files[0]).hasFileExtension("wav;wave;aif;aiff;flac;mp3;m4a");
}

void DivergeAudioProcessorEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() == 1) setAudioSlot(0, juce::File(files[0]));
}

void DivergeAudioProcessorEditor::chooseAudio(int slot)
{
    chooser = std::make_unique<juce::FileChooser>("Choose audio", juce::File {}, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.m4a");
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
    if (slot == 0) sourceButton.setButtonText(file.getFileName());
    else referenceButtons[static_cast<size_t>(slot - 1)].setButtonText(file.getFileName());
}

void DivergeAudioProcessorEditor::toggleCapture()
{
    if (!audioProcessor.isCapturing())
    {
        audioProcessor.beginCapture();
        recordButton.setButtonText("Stop & use recording");
    }
    else
    {
        const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                              .getNonexistentChildFile("diverge-capture", ".wav");
        if (audioProcessor.finishCapture(file)) setAudioSlot(0, file);
        recordButton.setButtonText("Record input");
    }
}

juce::String DivergeAudioProcessorEditor::styleHint() const
{
    return styleEditor.getText().trim();
}

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
            reference.add(referenceWeights[static_cast<size_t>(index)].getValue());
            references.add(reference);
        }
    object->setProperty("references", references);
    object->setProperty("transform", static_cast<int>(transformSlider.getValue()));
    object->setProperty("spread", static_cast<int>(spreadSlider.getValue()));
    object->setProperty("drift", static_cast<int>(driftSlider.getValue()));
    juce::Array<juce::var> locks;
    if (grooveLock.getToggleState()) locks.add("groove");
    if (melodyLock.getToggleState()) locks.add("melody");
    if (timbreLock.getToggleState()) locks.add("timbre");
    object->setProperty("locks", locks);
    object->setProperty("n_return", 8);
    object->setProperty("n_oversample", fastMode.getToggleState() ? 16 : 32);
    object->setProperty("duration_s", 8.0); object->setProperty("seed", juce::Random::getSystemRandom().nextInt());
    object->setProperty("library_index", libraryEditor.getText().trim());
    object->setProperty("critic_model", juce::File(modelsEditor.getText()).getChildFile("critic.joblib").getFullPathName());
    object->setProperty("choices_path", choicesFile().getFullPathName());
    object->setProperty("taste_events_path", tasteEventsFile().getFullPathName());
    object->setProperty("taste_model_path", tasteModelFile().getFullPathName());
    object->setProperty("opinion", static_cast<int>(opinionSlider.getValue()));
    object->setProperty("style_text_hint", styleHint()); object->setProperty("lock_threshold", 0.55);
    object->setProperty("fast", fastMode.getToggleState());
    object->setProperty("generation_batch_size", 8);
    object->setProperty("self_novelty_weight", 0.05);
    object->setProperty("output_dir", outputEditor.getText().trim());
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("diverge-run", ".json");
    file.replaceWithText(juce::JSON::toString(root, true));
    return file;
}

void DivergeAudioProcessorEditor::startGeneration()
{
    if (!audioSlots[0].existsAsFile()) { progressLabel.setText("Choose or record a source first.", juce::dontSendNotification); return; }
    saveSettings();
    const auto config = writeRunConfig();
    juce::StringArray command { pythonEditor.getText().trim(), "-m", "diverge.cli", "run", "--config",
                                quotedPath(config), "--models-dir", modelsEditor.getText().trim() };
    generateButton.setEnabled(false); cancelButton.setEnabled(true);
    progressLabel.setText("Starting local generator…", juce::dontSendNotification);
    juce::Component::SafePointer<DivergeAudioProcessorEditor> safeThis(this);
    job.start(command, juce::File(outputEditor.getText().trim()),
              [safeThis](const juce::String& progress)
              {
                  if (safeThis != nullptr)
                      safeThis->progressLabel.setText(progress, juce::dontSendNotification);
              },
              [safeThis](const juce::File& run, const juce::String& error)
              {
                  if (safeThis == nullptr) return;
                  safeThis->generateButton.setEnabled(true);
                  safeThis->cancelButton.setEnabled(false);
                  if (run.isDirectory())
                  {
                      safeThis->loadRun(run);
                      safeThis->progressLabel.setText("Generation complete", juce::dontSendNotification);
                  }
                  else
                      safeThis->progressLabel.setText(
                          error.isNotEmpty() ? error : "No completed run found.",
                          juce::dontSendNotification);
              });
}

void DivergeAudioProcessorEditor::loadRun(const juce::File& run)
{
    currentRun = run;
    const auto mapValue = juce::JSON::parse(run.getChildFile("map.json"));
    mapPoints.clear();
    if (auto* rows = mapValue.getArray())
        for (const auto& row : *rows)
            if (auto* item = row.getDynamicObject())
                mapPoints.push_back({ item->getProperty("kind").toString(), juce::File(item->getProperty("path").toString()),
                                      static_cast<float>(item->getProperty("x")), static_cast<float>(item->getProperty("y")),
                                      static_cast<int>(item->getProperty("rank")) });
    map.setPoints(mapPoints);
    const auto manifestValue = juce::JSON::parse(run.getChildFile("manifest.json"));
    if (auto* manifest = manifestValue.getDynamicObject())
        if (auto* rows = manifest->getProperty("candidates").getArray())
            for (const auto& row : *rows)
                if (auto* item = row.getDynamicObject())
                {
                    const auto rank = static_cast<int>(item->getProperty("rank"));
                    if (rank < 1 || rank > 8) continue;
                    const auto locks = item->getProperty("locks");
                    candidateDescriptions[static_cast<size_t>(rank - 1)] =
                        "Ref " + juce::String(static_cast<double>(item->getProperty("ref_fit")), 2)
                        + " · Groove " + juce::String(static_cast<double>(locks.getProperty("groove", 0.0)), 2)
                        + " · Novelty " + juce::String(static_cast<double>(item->getProperty("novelty")), 2)
                        + " · Taste " + juce::String(static_cast<double>(item->getProperty("taste")), 2)
                        + " · Utility " + juce::String(static_cast<double>(item->getProperty("utility")), 2);
                }
    for (int index = 0; index < 8; ++index)
    {
        candidates[static_cast<size_t>(index)] = run.getChildFile("cand_" + juce::String(index + 1).paddedLeft('0', 2) + ".wav");
        candidateButtons[static_cast<size_t>(index)].setEnabled(candidates[static_cast<size_t>(index)].existsAsFile());
    }
    selectCandidate(1);
}

void DivergeAudioProcessorEditor::selectCandidate(int rank)
{
    if (rank < 1 || rank > 8 || !candidates[static_cast<size_t>(rank - 1)].existsAsFile()) return;
    selectedCandidate = rank; map.setSelectedRank(rank);
    candidateDetail.setText("Candidate " + juce::String(rank) + " · "
                                + candidateDescriptions[static_cast<size_t>(rank - 1)],
                            juce::dontSendNotification);
    for (int index = 0; index < 8; ++index)
        candidateButtons[static_cast<size_t>(index)].setColour(juce::TextButton::buttonColourId,
            index + 1 == rank ? juce::Colours::orange : juce::Colour(panel));
    auditionButton.setEnabled(true); loveButton.setEnabled(true); keepButton.setEnabled(true);
    discardButton.setEnabled(true); undoButton.setEnabled(
        lastTasteEventIds[static_cast<size_t>(rank - 1)].isNotEmpty());
    dragButton.setEnabled(true);
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

void DivergeAudioProcessorEditor::recordDecision(const juce::String& label)
{
    if (selectedCandidate <= 0) return;
    runCriticCommand({ "add", candidates[static_cast<size_t>(selectedCandidate - 1)].getFullPathName(),
                       label, "--events", tasteEventsFile().getFullPathName(), "--model",
                       tasteModelFile().getFullPathName(), "--models-dir", modelsEditor.getText().trim(),
                       "--batch-id", currentRun.getFileName() });
    candidateDetail.setText("Candidate " + juce::String(selectedCandidate) + " · " + label, juce::dontSendNotification);
}

juce::File DivergeAudioProcessorEditor::choicesFile() const
{
    return juce::File(choicesEditor.getText().trim());
}

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
    tasteLabel.setText("taste model: updating…", juce::dontSendNotification);
    runCriticCommand({ "train", "--events", tasteEventsFile().getFullPathName(), "--model",
                       tasteModelFile().getFullPathName() });
}

void DivergeAudioProcessorEditor::pollCriticProcess()
{
    if (!decisionProcess || decisionProcess->isRunning()) return;
    const auto output = decisionProcess->readAllProcessOutput();
    decisionProcess.reset();
    if (criticAction == "add")
    {
        const auto parsed = juce::JSON::parse(output.substring(output.indexOfChar('{')));
        totalChoiceCount = static_cast<int>(parsed.getProperty("observations", 0));
        const auto eventId = parsed.getProperty("event_id", "").toString();
        for (int index = 0; index < 8; ++index)
            if (candidates[static_cast<size_t>(index)].getFullPathName() == criticCandidatePath)
                lastTasteEventIds[static_cast<size_t>(index)] = eventId;
        const auto confidence = static_cast<double>(parsed.getProperty("confidence", 0.0));
        tasteLabel.setText("taste: " + juce::String(totalChoiceCount) + " events · confidence "
                               + juce::String(static_cast<int>(confidence * 100.0)) + "% · v2",
                           juce::dontSendNotification);
        if (selectedCandidate > 0)
            undoButton.setEnabled(
                lastTasteEventIds[static_cast<size_t>(selectedCandidate - 1)].isNotEmpty());
    }
    else if (criticAction == "undo")
    {
        for (auto& eventId : lastTasteEventIds)
            if (eventId == criticCandidatePath) eventId.clear();
        tasteLabel.setText("taste: decision undone · v2", juce::dontSendNotification);
    }
    else if (criticAction == "train")
    {
        const auto jsonStart = output.indexOfChar('{');
        const auto parsed = juce::JSON::parse(jsonStart >= 0 ? output.substring(jsonStart) : output);
        const auto n = parsed.getProperty("observations", 0);
        totalChoiceCount = static_cast<int>(n);
        tasteLabel.setText("taste model: " + n.toString() + " choices",
                           juce::dontSendNotification);
        choicesSinceTraining = 0;
    }
    if (!criticQueue.empty())
    {
        auto next = criticQueue.front();
        criticQueue.pop_front();
        runCriticCommand(next);
    }
}

void DivergeAudioProcessorEditor::restoreSettings()
{
    auto& state = audioProcessor.state();
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
    opinionSlider.setValue(static_cast<double>(state.getProperty("opinion", 50)));
}

void DivergeAudioProcessorEditor::saveSettings()
{
    auto& state = audioProcessor.state();
    state.setProperty("python", pythonEditor.getText().trim(), nullptr);
    state.setProperty("models", modelsEditor.getText().trim(), nullptr);
    state.setProperty("library", libraryEditor.getText().trim(), nullptr);
    state.setProperty("choices", choicesEditor.getText().trim(), nullptr);
    state.setProperty("output", outputEditor.getText().trim(), nullptr);
    state.setProperty("opinion", opinionSlider.getValue(), nullptr);
}

void DivergeAudioProcessorEditor::timerCallback()
{
    pollCriticProcess();
    if (!audioProcessor.isCapturing() && recordButton.getButtonText().startsWith("Stop"))
        toggleCapture();
}
