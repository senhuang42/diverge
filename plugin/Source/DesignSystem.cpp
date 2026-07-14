#include "DesignSystem.h"

namespace DivergeTheme
{
juce::Font display(float size)
{
    return juce::Font(juce::FontOptions(size).withStyle("Bold")).withExtraKerningFactor(0.08f);
}

juce::Font label(float size)
{
    return juce::Font(juce::FontOptions(size).withStyle("Bold")).withExtraKerningFactor(0.14f);
}

juce::Font body(float size) { return juce::Font(juce::FontOptions(size)); }

juce::Font bodyBold(float size) { return juce::Font(juce::FontOptions(size).withStyle("Bold")); }

juce::Font mono(float size)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain));
}

juce::Font monoBold(float size)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, juce::Font::bold));
}

bool reducedMotion()
{
    static const bool value = juce::SystemStats::getEnvironmentVariable("DIVERGE_REDUCED_MOTION", {}) == "1";
    return value;
}

float approach(float current, float target, float rate)
{
    if (reducedMotion()) return target;
    const auto next = current + (target - current) * rate;
    return std::abs(next - target) < 0.004f ? target : next;
}

void paintInnerGlow(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                    juce::Colour colour, float intensity)
{
    for (int ring = 1; ring <= 3; ++ring)
    {
        g.setColour(colour.withAlpha(intensity * 0.09f / static_cast<float>(ring)));
        g.drawRoundedRectangle(bounds.reduced(static_cast<float>(ring)),
                               juce::jmax(2.0f, cornerRadius - static_cast<float>(ring)),
                               1.0f + static_cast<float>(ring));
    }
}
}

DivergeLookAndFeel::DivergeLookAndFeel()
{
    setColour(juce::TextButton::buttonColourId, DivergeTheme::raised);
    setColour(juce::TextButton::buttonOnColourId, DivergeTheme::explorationSoft);
    setColour(juce::TextButton::textColourOffId, DivergeTheme::text);
    setColour(juce::TextButton::textColourOnId, DivergeTheme::exploration);
    setColour(juce::Label::textColourId, DivergeTheme::text);
    setColour(juce::TextEditor::textColourId, DivergeTheme::text);
    setColour(juce::TextEditor::backgroundColourId, DivergeTheme::surface);
    setColour(juce::TextEditor::outlineColourId, DivergeTheme::hairline);
    setColour(juce::TextEditor::focusedOutlineColourId, DivergeTheme::exploration);
    setColour(juce::TextEditor::highlightColourId, DivergeTheme::explorationSoft);
    setColour(juce::Slider::textBoxTextColourId, DivergeTheme::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId, DivergeTheme::exploration);
}

DivergeLookAndFeel::~DivergeLookAndFeel() { stopTimer(); }

float DivergeLookAndFeel::animatedMix(juce::Component& component, float target)
{
    for (auto& state : hoverStates)
        if (state.component.getComponent() == &component)
        {
            state.target = target;
            if (state.value != state.target && !isTimerRunning()) startTimerHz(60);
            if (DivergeTheme::reducedMotion()) state.value = target;
            return state.value;
        }
    hoverStates.push_back({ &component, DivergeTheme::reducedMotion() ? target : 0.0f, target });
    if (target != hoverStates.back().value && !isTimerRunning()) startTimerHz(60);
    return hoverStates.back().value;
}

void DivergeLookAndFeel::timerCallback()
{
    bool anyAnimating = false;
    for (auto iterator = hoverStates.begin(); iterator != hoverStates.end();)
    {
        auto* component = iterator->component.getComponent();
        if (component == nullptr)
        {
            iterator = hoverStates.erase(iterator);
            continue;
        }
        if (iterator->value != iterator->target)
        {
            iterator->value = DivergeTheme::approach(iterator->value, iterator->target, 0.28f);
            component->repaint();
            if (iterator->value != iterator->target) anyAnimating = true;
        }
        ++iterator;
    }
    if (!anyAnimating) stopTimer();
}

void DivergeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                               const juce::Colour& background,
                                               bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const auto hover = animatedMix(button, button.isEnabled() && (highlighted || down) ? 1.0f : 0.0f);
    if (down) bounds = bounds.withTrimmedTop(1.0f);

    const auto isAccent = background == DivergeTheme::exploration || background == DivergeTheme::decision;
    auto colour = background;
    if (!button.isEnabled()) colour = DivergeTheme::surface;
    else colour = colour.brighter(hover * (isAccent ? 0.10f : 0.14f) + (down ? 0.05f : 0.0f));

    const auto corner = DivergeTheme::radius - 2.0f;
    g.setGradientFill({ colour.brighter(isAccent ? 0.06f : 0.05f), bounds.getX(), bounds.getY(),
                        colour.darker(0.10f), bounds.getX(), bounds.getBottom(), false });
    g.fillRoundedRectangle(bounds, corner);

    if (isAccent && button.isEnabled())
        DivergeTheme::paintInnerGlow(g, bounds, corner, juce::Colours::white, 0.6f + hover * 0.6f);

    const auto focused = button.hasKeyboardFocus(true);
    if (focused)
        DivergeTheme::paintInnerGlow(g, bounds, corner, DivergeTheme::exploration, 1.2f);
    g.setColour(focused ? DivergeTheme::exploration
                        : isAccent ? colour.brighter(0.25f).withAlpha(0.9f)
                                   : DivergeTheme::edge.withMultipliedAlpha(0.7f + hover * 0.3f));
    g.drawRoundedRectangle(bounds, corner, focused ? 1.5f : 1.0f);
}

void DivergeLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setColour(button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                          : juce::TextButton::textColourOffId)
                    .withMultipliedAlpha(button.isEnabled() ? 1.0f : 0.55f));
    g.setFont(DivergeTheme::bodyBold(13.5f));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(10, 2),
                     juce::Justification::centred, 1);
}

void DivergeLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto hover = animatedMix(button, button.isEnabled() && (highlighted || down) ? 1.0f : 0.0f);
    const auto on = button.getToggleState();
    const auto corner = bounds.getHeight() * 0.5f;

    auto fill = on ? DivergeTheme::explorationSoft : DivergeTheme::surface;
    g.setColour(fill.brighter(hover * 0.08f));
    g.fillRoundedRectangle(bounds, corner);
    if (on)
        DivergeTheme::paintInnerGlow(g, bounds, corner, DivergeTheme::exploration, 0.9f);
    const auto focused = button.hasKeyboardFocus(true);
    g.setColour(on || focused ? DivergeTheme::exploration.withAlpha(focused ? 1.0f : 0.75f)
                              : DivergeTheme::edge.withMultipliedAlpha(0.8f + hover * 0.2f));
    g.drawRoundedRectangle(bounds, corner, focused ? 1.5f : 1.1f);

    // Indicator dot leads the label so state reads without colour vision.
    auto content = button.getLocalBounds().reduced(14, 2).toFloat();
    const auto dot = juce::Rectangle<float>(7.0f, 7.0f).withCentre({ content.getX() + 4.0f, content.getCentreY() });
    if (on)
    {
        g.setColour(DivergeTheme::exploration);
        g.fillEllipse(dot);
        g.setColour(DivergeTheme::exploration.withAlpha(0.35f));
        g.drawEllipse(dot.expanded(2.5f), 1.2f);
    }
    else
    {
        g.setColour(DivergeTheme::dim);
        g.drawEllipse(dot, 1.2f);
    }
    g.setColour(on ? DivergeTheme::text : DivergeTheme::muted.brighter(hover * 0.25f));
    g.setFont(DivergeTheme::bodyBold(13.0f));
    g.drawFittedText(button.getButtonText(), content.withTrimmedLeft(14.0f).toNearestInt(),
                     juce::Justification::centred, 1);
}

void DivergeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosition, float, float,
                                           juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto hover = animatedMix(slider, slider.isMouseOverOrDragging() ? 1.0f : 0.0f);
    const auto track = juce::Rectangle<float>(static_cast<float>(x),
                                              static_cast<float>(y + height / 2) - 2.5f,
                                              static_cast<float>(width), 5.0f).reduced(10.0f, 0.0f);
    g.setColour(DivergeTheme::hairline.brighter(0.06f));
    g.fillRoundedRectangle(track, 2.5f);

    auto active = track.withWidth(juce::jmax(4.0f, sliderPosition - track.getX()));
    g.setGradientFill({ DivergeTheme::exploration.darker(0.25f), active.getX(), active.getY(),
                        DivergeTheme::exploration, active.getRight(), active.getY(), false });
    g.fillRoundedRectangle(active, 2.5f);

    const juce::Point<float> centre { sliderPosition, track.getCentreY() };
    g.setColour(DivergeTheme::exploration.withAlpha(0.12f + hover * 0.14f));
    g.fillEllipse(juce::Rectangle<float>(30.0f, 30.0f).withCentre(centre));
    g.setColour(DivergeTheme::text);
    g.fillEllipse(juce::Rectangle<float>(15.0f, 15.0f).withCentre(centre));
    g.setColour(DivergeTheme::exploration);
    g.drawEllipse(juce::Rectangle<float>(15.0f, 15.0f).withCentre(centre), 1.4f);
    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(DivergeTheme::exploration);
        g.drawEllipse(juce::Rectangle<float>(22.0f, 22.0f).withCentre(centre), 1.5f);
    }
}

void DivergeLookAndFeel::drawCornerResizer(juce::Graphics& g, int width, int height, bool, bool)
{
    g.setColour(DivergeTheme::dim.withAlpha(0.55f));
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);
    for (float inset = 1.0f; inset < 3.5f; inset += 1.0f)
        g.drawLine(w * inset * 0.3f, h - 2.0f, w - 2.0f, h * inset * 0.3f, 1.0f);
}

void DivergeLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                   static_cast<float>(height)), DivergeTheme::radius - 2.0f);
}

void DivergeLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1),
                                               static_cast<float>(height - 1));
    const auto focused = editor.hasKeyboardFocus(true);
    if (focused)
        DivergeTheme::paintInnerGlow(g, bounds, DivergeTheme::radius - 2.0f, DivergeTheme::exploration, 1.1f);
    g.setColour(focused ? DivergeTheme::exploration : DivergeTheme::hairline.brighter(0.08f));
    g.drawRoundedRectangle(bounds, DivergeTheme::radius - 2.0f, focused ? 1.4f : 1.0f);
}

ToastOverlay::ToastOverlay()
{
    setInterceptsMouseClicks(false, false);
    setVisible(false);
}

void ToastOverlay::show(juce::String message)
{
    text = std::move(message);
    ticksLeft = 160;
    if (DivergeTheme::reducedMotion()) slide = 1.0f;
    setVisible(true);
    toFront(false);
    startTimerHz(60);
    repaint();
}

void ToastOverlay::timerCallback()
{
    --ticksLeft;
    slide = DivergeTheme::approach(slide, ticksLeft > 18 ? 1.0f : 0.0f, 0.22f);
    if (ticksLeft <= 0 && slide < 0.02f)
    {
        slide = 0.0f;
        setVisible(false);
        stopTimer();
    }
    repaint();
}

void ToastOverlay::paint(juce::Graphics& g)
{
    if (slide <= 0.01f) return;
    const auto font = DivergeTheme::bodyBold(13.0f);
    juce::GlyphArrangement measure;
    measure.addLineOfText(font, text, 0.0f, 0.0f);
    const auto textWidth = measure.getBoundingBox(0, -1, true).getWidth();
    auto pill = getLocalBounds().toFloat().reduced(6.0f, 4.0f)
                    .translated(0.0f, (slide - 1.0f) * 12.0f);
    pill = pill.withSizeKeepingCentre(juce::jmin(pill.getWidth(), textWidth + 88.0f), pill.getHeight());
    const auto corner = pill.getHeight() * 0.5f;
    juce::Path shape;
    shape.addRoundedRectangle(pill, corner);
    juce::DropShadow(juce::Colours::black.withAlpha(0.45f * slide), 14, { 0, 4 }).drawForPath(g, shape);
    g.setColour(DivergeTheme::raised.withAlpha(0.98f * slide));
    g.fillPath(shape);
    g.setColour(DivergeTheme::exploration.withAlpha(0.55f * slide));
    g.drawRoundedRectangle(pill, corner, 1.0f);
    g.setColour(DivergeTheme::exploration.withAlpha(slide));
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre({ pill.getX() + 18.0f, pill.getCentreY() }));
    g.setColour(DivergeTheme::text.withAlpha(slide));
    g.setFont(font);
    g.drawFittedText(text, pill.toNearestInt().reduced(30, 2), juce::Justification::centred, 1);
}

void PanelSurface::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setGradientFill({ DivergeTheme::surface.brighter(0.03f), bounds.getX(), bounds.getY(),
                        DivergeTheme::surface, bounds.getX(), bounds.getBottom(), false });
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
    g.setColour(DivergeTheme::edge.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds, DivergeTheme::radius, 1.0f);
    g.setColour(juce::Colours::white.withAlpha(0.03f));
    g.drawRoundedRectangle(bounds.reduced(1.0f), DivergeTheme::radius - 1.0f, 1.0f);
}

void ScrimOverlay::paint(juce::Graphics& g)
{
    g.fillAll(DivergeTheme::canvas.withAlpha(0.6f));
}

void ScrimOverlay::mouseDown(const juce::MouseEvent&)
{
    if (onDismiss) onDismiss();
}

WaveformCard::WaveformCard(juce::AudioFormatManager& manager, juce::AudioThumbnailCache& cache)
    : thumbnail(256, manager, cache)
{
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

void WaveformCard::setAudio(juce::String title, juce::String empty, const juce::File& file)
{
    heading = std::move(title);
    prompt = std::move(empty);
    audioFile = file;
    thumbnail.clear();
    if (audioFile.existsAsFile())
        thumbnail.setSource(new juce::FileInputSource(audioFile));
    setTitle(heading.isNotEmpty() ? heading : prompt);
    setDescription(audioFile.existsAsFile() ? audioFile.getFileName() : prompt);
    repaint();
}

void WaveformCard::setSupportingText(juce::String next)
{
    if (supportingText == next) return;
    supportingText = std::move(next);
    repaint();
}

void WaveformCard::setState(bool isSelected, bool isPlaying, double progress, CandidateDecision nextDecision)
{
    const auto clamped = juce::jlimit(0.0, 1.0, progress);
    if (selected == isSelected && playing == isPlaying && decision == nextDecision
        && std::abs(playbackProgress - clamped) < 0.0005)
        return;
    selected = isSelected;
    playing = isPlaying;
    playbackProgress = clamped;
    decision = nextDecision;
    repaint();
}

void WaveformCard::beginEntrance(int order)
{
    if (DivergeTheme::reducedMotion())
    {
        entrance = 1.0f;
        entranceDelay = 0;
        setAlpha(1.0f);
        return;
    }
    entrance = 0.0f;
    entranceDelay = order * 3;
    setAlpha(0.0f);
}

void WaveformCard::advanceAnimation()
{
    if (entranceDelay > 0)
        --entranceDelay;
    else if (entrance < 1.0f)
    {
        entrance = DivergeTheme::approach(entrance, 1.0f, 0.16f);
        setAlpha(entrance);
    }
    const auto target = hovered || selected ? 1.0f : 0.0f;
    const auto next = DivergeTheme::approach(hoverMix, target, 0.26f);
    if (std::abs(next - hoverMix) > 0.002f)
    {
        hoverMix = next;
        repaint();
    }
}

void WaveformCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto focused = hasKeyboardFocus(true);
    const auto active = selected || focused;
    const auto corner = DivergeTheme::radius;

    const auto lift = 0.35f + hoverMix * 0.65f;
    g.setGradientFill({ DivergeTheme::surface.interpolatedWith(DivergeTheme::raised, lift * 0.9f),
                        bounds.getX(), bounds.getY(),
                        DivergeTheme::surface.interpolatedWith(DivergeTheme::raised, hoverMix * 0.4f),
                        bounds.getX(), bounds.getBottom(), false });
    g.fillRoundedRectangle(bounds, corner);
    if (selected)
    {
        g.setGradientFill({ DivergeTheme::exploration.withAlpha(0.10f), bounds.getX(), bounds.getY(),
                            juce::Colours::transparentBlack, bounds.getX(),
                            bounds.getY() + bounds.getHeight() * 0.75f, false });
        g.fillRoundedRectangle(bounds, corner);
    }
    if (active)
        DivergeTheme::paintInnerGlow(g, bounds, corner, DivergeTheme::exploration, selected ? 1.0f : 0.55f);
    g.setColour(active ? DivergeTheme::exploration
                       : DivergeTheme::edge.withMultipliedAlpha(0.65f + hoverMix * 0.35f));
    g.drawRoundedRectangle(bounds, corner, active ? 1.6f : 1.0f);

    auto content = getLocalBounds().reduced(14, 9);
    juce::String badge;
    juce::Colour badgeColour = DivergeTheme::muted;
    if (decision == CandidateDecision::keep) { badge = "KEPT"; badgeColour = DivergeTheme::decision; }
    else if (decision == CandidateDecision::pass) { badge = "PASSED"; badgeColour = DivergeTheme::pass; }
    else if (decision == CandidateDecision::favorite) { badge = "FAVORITE"; badgeColour = DivergeTheme::decision; }
    else if (decision == CandidateDecision::exported) { badge = "IN DAW"; badgeColour = DivergeTheme::exploration; }

    if (heading.isNotEmpty() || badge.isNotEmpty())
    {
        auto header = content.removeFromTop(18);
        const auto numbered = heading.containsOnly("0123456789");
        g.setFont(numbered ? DivergeTheme::monoBold(11.0f) : DivergeTheme::label(10.5f));
        g.setColour(selected ? DivergeTheme::exploration : DivergeTheme::muted);
        g.drawText(heading.toUpperCase(), header, juce::Justification::centredLeft, false);
        if (badge.isNotEmpty())
        {
            const auto badgeWidth = badge.length() * 7 + 20;
            auto pill = header.removeFromRight(badgeWidth).toFloat().withSizeKeepingCentre(
                static_cast<float>(badgeWidth), 16.0f);
            g.setColour(badgeColour.withAlpha(0.16f));
            g.fillRoundedRectangle(pill, 8.0f);
            g.setColour(badgeColour.withAlpha(0.5f));
            g.drawRoundedRectangle(pill, 8.0f, 1.0f);
            g.setColour(badgeColour);
            g.setFont(DivergeTheme::monoBold(9.0f));
            g.drawText(badge, pill.toNearestInt(), juce::Justification::centred, false);
        }
        content.removeFromTop(2);
    }

    if (!audioFile.existsAsFile())
    {
        juce::Path outline;
        outline.addRoundedRectangle(bounds.reduced(6.0f), corner - 5.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 5.0f };
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(DivergeTheme::edge.interpolatedWith(DivergeTheme::exploration, hoverMix * 0.7f)
                        .withAlpha(0.55f + hoverMix * 0.45f));
        g.fillPath(dashed);

        auto empty = content;
        auto plusArea = empty.removeFromLeft(34).withSizeKeepingCentre(26, 26).toFloat();
        g.setColour(DivergeTheme::muted.interpolatedWith(DivergeTheme::exploration, hoverMix));
        g.drawEllipse(plusArea, 1.3f);
        g.fillRect(plusArea.getCentreX() - 5.0f, plusArea.getCentreY() - 0.75f, 10.0f, 1.5f);
        g.fillRect(plusArea.getCentreX() - 0.75f, plusArea.getCentreY() - 5.0f, 1.5f, 10.0f);
        empty.removeFromLeft(8);
        g.setColour(DivergeTheme::muted.brighter(hoverMix * 0.3f));
        g.setFont(DivergeTheme::body(13.5f));
        g.drawFittedText(prompt, empty, juce::Justification::centredLeft, 2);
        return;
    }

    auto playArea = content.removeFromLeft(34).withSizeKeepingCentre(28, 28).toFloat();
    playArea = playArea.expanded(hoverMix * 1.0f);
    if (playing)
    {
        g.setColour(DivergeTheme::exploration.withAlpha(0.25f));
        g.drawEllipse(playArea.expanded(3.0f), 1.2f);
        g.setColour(DivergeTheme::exploration);
        g.fillEllipse(playArea);
        g.setColour(DivergeTheme::canvas);
        g.fillRect(playArea.getCentreX() - 4.5f, playArea.getCentreY() - 5.0f, 3.0f, 10.0f);
        g.fillRect(playArea.getCentreX() + 1.5f, playArea.getCentreY() - 5.0f, 3.0f, 10.0f);
    }
    else
    {
        g.setColour(DivergeTheme::raised.brighter(0.1f + hoverMix * 0.15f));
        g.fillEllipse(playArea);
        g.setColour(DivergeTheme::edge.interpolatedWith(DivergeTheme::exploration, hoverMix));
        g.drawEllipse(playArea, 1.2f);
        juce::Path play;
        play.addTriangle(playArea.getCentreX() - 3.5f, playArea.getCentreY() - 5.5f,
                         playArea.getCentreX() - 3.5f, playArea.getCentreY() + 5.5f,
                         playArea.getCentreX() + 6.0f, playArea.getCentreY());
        g.setColour(DivergeTheme::text);
        g.fillPath(play);
    }
    content.removeFromLeft(8);

    auto waveform = content;
    if (supportingText.isNotEmpty())
    {
        auto detail = waveform.removeFromBottom(17);
        g.setColour(DivergeTheme::muted.withAlpha(0.9f));
        g.setFont(DivergeTheme::body(11.0f));
        g.drawFittedText(supportingText, detail, juce::Justification::centredLeft, 1);
    }
    if (thumbnail.getTotalLength() > 0.0)
    {
        // Idle waveform, then the played portion re-drawn in accent so audio "ignites".
        g.setColour(selected ? DivergeTheme::text.withAlpha(0.55f) : DivergeTheme::muted.withAlpha(0.55f));
        thumbnail.drawChannels(g, waveform, 0.0, thumbnail.getTotalLength(), 0.92f);
        const auto playheadX = waveform.getX()
                               + static_cast<int>(playbackProgress * waveform.getWidth());
        if (playing)
        {
            {
                juce::Graphics::ScopedSaveState clip(g);
                g.reduceClipRegion(waveform.withRight(playheadX));
                g.setGradientFill({ DivergeTheme::exploration.darker(0.15f),
                                    static_cast<float>(waveform.getX()), 0.0f,
                                    DivergeTheme::exploration, static_cast<float>(playheadX), 0.0f, false });
                thumbnail.drawChannels(g, waveform, 0.0, thumbnail.getTotalLength(), 0.92f);
            }
            g.setColour(DivergeTheme::exploration.withAlpha(0.18f));
            g.fillRect(playheadX - 3, waveform.getY(), 7, waveform.getHeight());
            g.setColour(DivergeTheme::text);
            g.fillRect(playheadX, waveform.getY(), 1, waveform.getHeight());
            g.setColour(DivergeTheme::exploration);
            g.fillEllipse(juce::Rectangle<float>(5.0f, 5.0f).withCentre(
                { static_cast<float>(playheadX) + 0.5f, static_cast<float>(waveform.getY()) + 2.0f }));
        }
    }
    else
    {
        g.setColour(DivergeTheme::hairline.brighter(0.1f));
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
