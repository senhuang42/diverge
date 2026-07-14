#include "DesignSystem.h"

DivergeLookAndFeel::DivergeLookAndFeel()
{
    setColour(juce::TextButton::buttonColourId, DivergeTheme::raised);
    setColour(juce::TextButton::buttonOnColourId, DivergeTheme::explorationSoft);
    setColour(juce::TextButton::textColourOffId, DivergeTheme::text);
    setColour(juce::TextButton::textColourOnId, DivergeTheme::text);
    setColour(juce::Label::textColourId, DivergeTheme::text);
    setColour(juce::TextEditor::textColourId, DivergeTheme::text);
    setColour(juce::TextEditor::backgroundColourId, DivergeTheme::surface);
    setColour(juce::TextEditor::outlineColourId, DivergeTheme::edge);
    setColour(juce::TextEditor::focusedOutlineColourId, DivergeTheme::exploration);
    setColour(juce::TextEditor::highlightColourId, DivergeTheme::explorationSoft);
    setColour(juce::Slider::textBoxTextColourId, DivergeTheme::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

void DivergeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& background,
                                               bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto colour = background;
    if (!button.isEnabled()) colour = DivergeTheme::surface;
    else if (down) colour = colour.brighter(0.16f);
    else if (highlighted) colour = colour.brighter(0.09f);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
    g.setColour(button.hasKeyboardFocus(true) ? DivergeTheme::exploration : DivergeTheme::edge);
    g.drawRoundedRectangle(bounds, DivergeTheme::radius, button.hasKeyboardFocus(true) ? 1.5f : 1.0f);
}

void DivergeLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                          : juce::TextButton::textColourOffId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.4f));
    g.setFont(juce::FontOptions(14.0f).withStyle("Bold"));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(10, 2),
                     juce::Justification::centred, 1);
}

void DivergeLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    auto fill = button.getToggleState() ? DivergeTheme::explorationSoft : DivergeTheme::surface;
    if (highlighted || down) fill = fill.brighter(0.08f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
    g.setColour(button.getToggleState() ? DivergeTheme::exploration : DivergeTheme::edge);
    g.drawRoundedRectangle(bounds, bounds.getHeight() * 0.5f, 1.0f);
    g.setColour(button.getToggleState() ? DivergeTheme::text : DivergeTheme::muted);
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawFittedText((button.getToggleState() ? juce::String("ON  ") : juce::String()) + button.getButtonText(),
                     button.getLocalBounds().reduced(12, 2), juce::Justification::centred, 1);
}

void DivergeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosition, float, float,
                                           juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto track = juce::Rectangle<float>(static_cast<float>(x), static_cast<float>(y + height / 2 - 2),
                                               static_cast<float>(width), 4.0f).reduced(10.0f, 0.0f);
    g.setColour(DivergeTheme::edge);
    g.fillRoundedRectangle(track, 2.0f);
    g.setColour(DivergeTheme::exploration);
    g.fillRoundedRectangle(track.withWidth(juce::jmax(2.0f, sliderPosition - track.getX())), 2.0f);
    g.setColour(DivergeTheme::text);
    g.fillEllipse(juce::Rectangle<float>(16.0f, 16.0f).withCentre({ sliderPosition, track.getCentreY() }));
    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(DivergeTheme::exploration);
        g.drawEllipse(juce::Rectangle<float>(22.0f, 22.0f).withCentre({ sliderPosition, track.getCentreY() }), 1.5f);
    }
}

void DivergeLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                   static_cast<float>(height)), DivergeTheme::radius);
}

void DivergeLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.hasKeyboardFocus(true) ? DivergeTheme::exploration : DivergeTheme::edge);
    g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1),
                                                   static_cast<float>(height - 1)),
                           DivergeTheme::radius, editor.hasKeyboardFocus(true) ? 1.5f : 1.0f);
}

WaveformCard::WaveformCard(juce::AudioFormatManager& manager, juce::AudioThumbnailCache& cache)
    : thumbnail(256, manager, cache)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
    reducedMotion = juce::SystemStats::getEnvironmentVariable("DIVERGE_REDUCED_MOTION", {}) == "1";
}

void WaveformCard::setAudio(juce::String title, juce::String empty, const juce::File& file)
{
    heading = std::move(title);
    prompt = std::move(empty);
    audioFile = file;
    thumbnail.clear();
    if (audioFile.existsAsFile())
        thumbnail.setSource(new juce::FileInputSource(audioFile));
    setTitle(heading);
    setDescription(audioFile.existsAsFile() ? audioFile.getFileName() : prompt);
    repaint();
}

void WaveformCard::setSupportingText(juce::String text)
{
    supportingText = std::move(text);
    repaint();
}

void WaveformCard::setState(bool isSelected, bool isPlaying, double progress, CandidateDecision nextDecision)
{
    selected = isSelected;
    playing = isPlaying;
    playbackProgress = juce::jlimit(0.0, 1.0, progress);
    decision = nextDecision;
    repaint();
}

void WaveformCard::advanceAnimation()
{
    const auto target = hovered || selected ? 1.0f : 0.0f;
    const auto next = reducedMotion ? target : hoverMix + (target - hoverMix) * 0.28f;
    if (std::abs(next - hoverMix) > 0.002f)
    {
        hoverMix = next;
        repaint();
    }
}

void WaveformCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto fill = DivergeTheme::surface.interpolatedWith(DivergeTheme::raised, hoverMix * 0.8f);
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
    const auto focused = hasKeyboardFocus(true);
    g.setColour(selected || focused ? DivergeTheme::exploration
                                    : DivergeTheme::edge.withMultipliedAlpha(0.8f));
    g.drawRoundedRectangle(bounds, DivergeTheme::radius, selected || focused ? 1.7f : 1.0f);

    auto content = getLocalBounds().reduced(14, 9);
    auto header = content.removeFromTop(20);
    g.setFont(juce::FontOptions(12.5f).withStyle("Bold"));
    g.setColour(selected ? DivergeTheme::exploration : DivergeTheme::text);
    g.drawText(heading.toUpperCase(), header.removeFromLeft(120), juce::Justification::centredLeft, false);

    juce::String badge;
    juce::Colour badgeColour = DivergeTheme::muted;
    if (decision == CandidateDecision::keep) { badge = "KEPT"; badgeColour = DivergeTheme::decision; }
    else if (decision == CandidateDecision::pass) { badge = "PASSED"; badgeColour = DivergeTheme::pass; }
    else if (decision == CandidateDecision::favorite) { badge = "FAVORITE"; badgeColour = DivergeTheme::decision; }
    else if (decision == CandidateDecision::exported) { badge = "USED"; badgeColour = DivergeTheme::decision; }
    if (badge.isNotEmpty())
    {
        g.setColour(badgeColour);
        g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
        g.drawText(badge, header, juce::Justification::centredRight, false);
    }

    auto playArea = content.removeFromLeft(32).withSizeKeepingCentre(28, 28);
    g.setColour(playing ? DivergeTheme::exploration : DivergeTheme::edge);
    g.fillEllipse(playArea.toFloat());
    g.setColour(playing ? DivergeTheme::canvas : DivergeTheme::text);
    if (playing)
        g.fillRect(playArea.withSizeKeepingCentre(8, 10).toFloat());
    else
    {
        juce::Path play;
        play.addTriangle(static_cast<float>(playArea.getX() + 10), static_cast<float>(playArea.getY() + 7),
                         static_cast<float>(playArea.getX() + 10), static_cast<float>(playArea.getBottom() - 7),
                         static_cast<float>(playArea.getRight() - 7), static_cast<float>(playArea.getCentreY()));
        g.fillPath(play);
    }
    content.removeFromLeft(8);

    if (!audioFile.existsAsFile())
    {
        g.setColour(DivergeTheme::muted);
        g.setFont(juce::FontOptions(14.0f));
        g.drawFittedText(prompt, content, juce::Justification::centredLeft, 1);
        return;
    }

    auto waveform = content;
    if (supportingText.isNotEmpty())
    {
        auto detail = waveform.removeFromBottom(18);
        g.setColour(DivergeTheme::muted);
        g.setFont(juce::FontOptions(11.5f));
        g.drawFittedText(supportingText, detail, juce::Justification::centredLeft, 1);
    }
    if (thumbnail.getTotalLength() > 0.0)
    {
        g.setColour(selected ? DivergeTheme::exploration.withAlpha(0.82f)
                             : DivergeTheme::muted.withAlpha(0.68f));
        thumbnail.drawChannels(g, waveform, 0.0, thumbnail.getTotalLength(), 0.9f);
        if (playing)
        {
            const auto playhead = waveform.getX() + static_cast<int>(playbackProgress * waveform.getWidth());
            g.setColour(DivergeTheme::text);
            g.fillRect(playhead, waveform.getY(), 1, waveform.getHeight());
        }
    }
    else
    {
        g.setColour(DivergeTheme::edge);
        g.drawHorizontalLine(waveform.getCentreY(), static_cast<float>(waveform.getX()),
                             static_cast<float>(waveform.getRight()));
    }
}

void WaveformCard::mouseEnter(const juce::MouseEvent&) { hovered = true; }
void WaveformCard::mouseExit(const juce::MouseEvent&) { hovered = false; }

void WaveformCard::mouseDown(const juce::MouseEvent& event)
{
    if (audioFile.existsAsFile())
    {
        if (onSeek && event.x > 54)
            onSeek(juce::jlimit(0.0, 1.0, static_cast<double>(event.x - 54)
                                           / static_cast<double>(juce::jmax(1, getWidth() - 68))));
        else if (onActivate)
            onActivate();
    }
    else if (onChoose)
        onChoose();
}

bool WaveformCard::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        if (audioFile.existsAsFile() && onActivate) onActivate();
        else if (!audioFile.existsAsFile() && onChoose) onChoose();
        return true;
    }
    return false;
}

void WaveformCard::mouseDrag(const juce::MouseEvent& event)
{
    if (draggable && event.getDistanceFromDragStart() > 8 && onDrag)
        onDrag();
}
