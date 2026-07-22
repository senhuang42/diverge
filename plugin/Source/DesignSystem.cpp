#include "DesignSystem.h"

namespace DivergeTheme
{
namespace
{
// A workhorse grotesque carries an Operate surface better than a display face with opinions.
// The contact-sheet voice comes from the marks and the layout, not from the letterforms.
const juce::String& uiTypeface()
{
    static const juce::String name = []
    {
        const auto available = juce::Font::findAllTypefaceNames();
        for (const auto* candidate : { "Helvetica Neue", "Helvetica", "Arial" })
            if (available.contains(candidate)) return juce::String(candidate);
        return juce::Font::getDefaultSansSerifFontName();
    }();
    return name;
}

juce::Font make(float size, const juce::String& style)
{
    return juce::Font(juce::FontOptions(uiTypeface(), size, juce::Font::plain).withStyle(style));
}
}

// Tracking floor is -0.04em; -0.02em reads better at display size and is where this stops.
juce::Font display(float size) { return make(size, "Bold").withExtraKerningFactor(-0.02f); }

juce::Font title(float size) { return make(size, "Bold").withExtraKerningFactor(-0.01f); }

// Tracked caps are a named kicker, not section grammar. Used for the transport legend and
// the take badges only.
juce::Font label(float size) { return make(size, "Bold").withExtraKerningFactor(0.12f); }

juce::Font body(float size) { return make(size, "Regular"); }

juce::Font bodyBold(float size) { return make(size, "Bold"); }

// Monospace is reserved for what it is for: take numbers, timings, and filesystem paths.
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

void paintDropShadow(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius,
                     float blur, float offsetY, float opacity)
{
    juce::Path shape;
    shape.addRoundedRectangle(bounds, cornerRadius);
    juce::DropShadow(juce::Colours::black.withAlpha(opacity),
                     juce::roundToInt(blur), { 0, juce::roundToInt(offsetY) })
        .drawForPath(g, shape);
}

void paintFocusRing(juce::Graphics& g, juce::Rectangle<float> bounds, float cornerRadius)
{
    g.setColour(canvas);
    g.drawRoundedRectangle(bounds.expanded(1.5f), cornerRadius + 1.5f, 3.0f);
    g.setColour(text);
    g.drawRoundedRectangle(bounds.expanded(1.5f), cornerRadius + 1.5f, 1.5f);
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
    setColour(juce::TextEditor::backgroundColourId, DivergeTheme::canvas);
    setColour(juce::TextEditor::outlineColourId, DivergeTheme::edge);
    setColour(juce::TextEditor::focusedOutlineColourId, DivergeTheme::text);
    setColour(juce::TextEditor::highlightColourId, DivergeTheme::explorationSoft);
    setColour(juce::Slider::textBoxTextColourId, DivergeTheme::text);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::CaretComponent::caretColourId, DivergeTheme::exploration);
    setColour(juce::PopupMenu::backgroundColourId, DivergeTheme::raised);
    setColour(juce::PopupMenu::textColourId, DivergeTheme::text);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, DivergeTheme::explorationSoft);
    setColour(juce::PopupMenu::highlightedTextColourId, DivergeTheme::text);
    setColour(juce::ComboBox::backgroundColourId, DivergeTheme::raised);
    setColour(juce::ComboBox::textColourId, DivergeTheme::text);
    setColour(juce::ComboBox::outlineColourId, DivergeTheme::edge);
    setColour(juce::ComboBox::arrowColourId, DivergeTheme::muted);
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
    const auto corner = juce::jmin(9.0f, bounds.getHeight() * 0.32f);
    const auto filled = background == DivergeTheme::exploration;

    if (!button.isEnabled())
    {
        // A disabled control never competes for a click, but it still has to be readable. A
        // filled action keeps a muted fill so its label survives; an outline states its edge.
        if (filled)
        {
            g.setColour(DivergeTheme::hairline);
            g.fillRoundedRectangle(bounds, corner);
        }
        else
        {
            g.setColour(DivergeTheme::hairline.withAlpha(0.5f));
            g.drawRoundedRectangle(bounds, corner, 1.0f);
        }
        return;
    }

    if (filled)
    {
        // The one decisive act on the surface. Flat chinagraph, no gradient, no halo: it reads
        // as a mark laid down rather than as a lozenge of chrome.
        DivergeTheme::paintDropShadow(g, bounds.reduced(2.0f), corner, 12.0f, 3.0f, 0.4f);
        g.setColour(background.brighter(hover * 0.12f + (down ? 0.06f : 0.0f)));
        g.fillRoundedRectangle(bounds, corner);
    }
    else
    {
        // Secondary controls are outlines on the ground. Elevation is declared once, as a rule.
        g.setColour(DivergeTheme::surface.withAlpha(0.5f + hover * 0.5f));
        g.fillRoundedRectangle(bounds, corner);
        g.setColour(DivergeTheme::edge.interpolatedWith(DivergeTheme::muted, hover * 0.6f));
        g.drawRoundedRectangle(bounds, corner, 1.0f);
    }

    if (button.hasKeyboardFocus(true))
        DivergeTheme::paintFocusRing(g, bounds, corner);
}

void DivergeLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto filled = button.findColour(juce::TextButton::buttonColourId) == DivergeTheme::exploration;
    // A filled action carries a dark label for contrast against chinagraph. That label would
    // vanish once the fill is gone, so a disabled button always falls back to muted.
    g.setColour(button.isEnabled()
                    ? button.findColour(button.getToggleState() ? juce::TextButton::textColourOnId
                                                                : juce::TextButton::textColourOffId)
                    : DivergeTheme::muted.withMultipliedAlpha(0.75f));
    g.setFont(DivergeTheme::bodyBold(filled ? DivergeTheme::Type::body + 0.5f : DivergeTheme::Type::body));
    g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(12, 2),
                     juce::Justification::centred, 1);
}

void DivergeLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                           bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
    const auto hover = animatedMix(button, button.isEnabled() && (highlighted || down) ? 1.0f : 0.0f);
    const auto on = button.getToggleState();
    const auto corner = 9.0f;

    // A preserve lock is held or it is not. Held reads as paper: the frame is lit, not tinted.
    g.setColour(on ? DivergeTheme::raised.brighter(0.06f + hover * 0.05f)
                   : DivergeTheme::surface.withAlpha(0.45f + hover * 0.4f));
    g.fillRoundedRectangle(bounds, corner);
    g.setColour(on ? DivergeTheme::text.withAlpha(0.85f)
                   : DivergeTheme::edge.interpolatedWith(DivergeTheme::muted, hover * 0.6f));
    g.drawRoundedRectangle(bounds, corner, on ? 1.4f : 1.0f);

    // The state is a filled or open square, so it survives without colour vision.
    auto content = bounds.reduced(15.0f, 0.0f);
    const auto box = juce::Rectangle<float>(9.0f, 9.0f)
                         .withCentre({ content.getX() + 4.5f, content.getCentreY() });
    if (on)
    {
        g.setColour(DivergeTheme::text);
        g.fillRoundedRectangle(box, 2.0f);
        g.setColour(DivergeTheme::canvas);
        juce::Path tick;
        tick.startNewSubPath(box.getX() + 2.2f, box.getCentreY());
        tick.lineTo(box.getCentreX() - 0.4f, box.getBottom() - 2.4f);
        tick.lineTo(box.getRight() - 1.8f, box.getY() + 2.4f);
        g.strokePath(tick, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));
    }
    else
    {
        g.setColour(DivergeTheme::dim.brighter(hover * 0.3f));
        g.drawRoundedRectangle(box, 2.0f, 1.3f);
    }

    g.setColour(on ? DivergeTheme::text : DivergeTheme::muted.brighter(hover * 0.25f));
    g.setFont(DivergeTheme::bodyBold(DivergeTheme::Type::body));
    g.drawFittedText(button.getButtonText(), content.withTrimmedLeft(16.0f).toNearestInt(),
                     juce::Justification::centred, 1);

    if (button.hasKeyboardFocus(true))
        DivergeTheme::paintFocusRing(g, bounds, corner);
}

void DivergeLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                           float sliderPosition, float, float,
                                           juce::Slider::SliderStyle, juce::Slider& slider)
{
    const auto hover = animatedMix(slider, slider.isMouseOverOrDragging() ? 1.0f : 0.0f);
    const auto track = juce::Rectangle<float>(static_cast<float>(x),
                                              static_cast<float>(y + height / 2) - 1.5f,
                                              static_cast<float>(width), 3.0f).reduced(11.0f, 0.0f);
    g.setColour(DivergeTheme::hairline);
    g.fillRoundedRectangle(track, 1.5f);

    // The travelled part of the range is the mark the hand has made on the dial.
    auto active = track.withWidth(juce::jmax(3.0f, sliderPosition - track.getX()));
    g.setColour(DivergeTheme::exploration);
    g.fillRoundedRectangle(active, 1.5f);

    const juce::Point<float> centre { sliderPosition, track.getCentreY() };
    const auto knob = juce::Rectangle<float>(18.0f, 18.0f).withCentre(centre);
    DivergeTheme::paintDropShadow(g, knob.reduced(2.0f), 9.0f, 8.0f, 2.0f, 0.45f);
    g.setColour(DivergeTheme::text);
    g.fillEllipse(knob.reduced(2.0f - hover * 0.8f));
    g.setColour(DivergeTheme::exploration);
    g.fillEllipse(juce::Rectangle<float>(6.0f, 6.0f).withCentre(centre));
    if (slider.hasKeyboardFocus(true))
    {
        g.setColour(DivergeTheme::text);
        g.drawEllipse(knob.expanded(3.0f), 1.5f);
    }
}

void DivergeLookAndFeel::drawCornerResizer(juce::Graphics& g, int width, int height, bool, bool)
{
    g.setColour(DivergeTheme::dim.withAlpha(0.5f));
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);
    for (float inset = 1.0f; inset < 3.5f; inset += 1.0f)
        g.drawLine(w * inset * 0.3f, h - 2.0f, w - 2.0f, h * inset * 0.3f, 1.0f);
}

void DivergeLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle(juce::Rectangle<float>(0.0f, 0.0f, static_cast<float>(width),
                                                   static_cast<float>(height)), 9.0f);
}

void DivergeLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto bounds = juce::Rectangle<float>(0.5f, 0.5f, static_cast<float>(width - 1),
                                               static_cast<float>(height - 1));
    const auto focused = editor.hasKeyboardFocus(true);
    g.setColour(focused ? DivergeTheme::text : DivergeTheme::edge);
    g.drawRoundedRectangle(bounds, 9.0f, focused ? 1.6f : 1.0f);
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
    const auto font = DivergeTheme::bodyBold(DivergeTheme::Type::body);
    juce::GlyphArrangement measure;
    measure.addLineOfText(font, text, 0.0f, 0.0f);
    const auto textWidth = measure.getBoundingBox(0, -1, true).getWidth();
    auto pill = getLocalBounds().toFloat().reduced(6.0f, 4.0f)
                    .translated(0.0f, (1.0f - slide) * 14.0f);
    pill = pill.withSizeKeepingCentre(juce::jmin(pill.getWidth(), textWidth + 60.0f), pill.getHeight());
    const auto corner = pill.getHeight() * 0.5f;
    DivergeTheme::paintDropShadow(g, pill, corner, 20.0f, 6.0f, 0.5f * slide);
    g.setColour(DivergeTheme::raised.brighter(0.08f).withAlpha(slide));
    g.fillRoundedRectangle(pill, corner);
    g.setColour(DivergeTheme::text.withAlpha(slide));
    g.setFont(font);
    g.drawFittedText(text, pill.toNearestInt().reduced(24, 2), juce::Justification::centred, 1);
}

void PanelSurface::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    // A panel genuinely floats, so it takes the shadow and states no border.
    DivergeTheme::paintDropShadow(g, bounds, DivergeTheme::radius, 28.0f, 10.0f, 0.55f);
    g.setColour(DivergeTheme::surface);
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
}

void StatusCard::set(juce::String nextTitle, juce::String nextBody, State nextState)
{
    if (title == nextTitle && body == nextBody && state == nextState) return;
    title = std::move(nextTitle);
    body = std::move(nextBody);
    state = nextState;
    setTitle(title);
    setDescription(body);
    repaint();
}

void StatusCard::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    g.setColour(DivergeTheme::canvas.withAlpha(0.6f));
    g.fillRoundedRectangle(bounds, DivergeTheme::radius);
    g.setColour(state == State::attention ? DivergeTheme::decision.withAlpha(0.7f) : DivergeTheme::hairline);
    g.drawRoundedRectangle(bounds, DivergeTheme::radius, 1.0f);

    auto content = getLocalBounds().reduced(16, 14);
    auto titleRow = content.removeFromTop(18);
    // Filled dot means the subsystem has something to say; open means it is simply idle.
    const auto tint = state == State::ok ? DivergeTheme::text
                    : state == State::attention ? DivergeTheme::decision
                                                : DivergeTheme::dim;
    const auto dot = juce::Rectangle<float>(7.0f, 7.0f)
                         .withCentre({ static_cast<float>(titleRow.getX()) + 3.5f,
                                       static_cast<float>(titleRow.getCentreY()) });
    g.setColour(tint);
    if (state == State::neutral) g.drawEllipse(dot, 1.2f);
    else g.fillEllipse(dot);
    g.setColour(DivergeTheme::text);
    g.setFont(DivergeTheme::bodyBold(DivergeTheme::Type::body));
    g.drawText(title, titleRow.withTrimmedLeft(16), juce::Justification::centredLeft, false);
    content.removeFromTop(6);
    g.setColour(state == State::attention ? DivergeTheme::text : DivergeTheme::muted);
    g.setFont(DivergeTheme::body(DivergeTheme::Type::meta));
    g.drawFittedText(body, content, juce::Justification::topLeft, 3);
}

void ScrimOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f));
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

// The grease pencil. Drawn slightly off-square and with an open stroke so it reads as a hand
// mark on top of the frame rather than as another piece of chrome inside it. Shape alone says
// which decision was made, which is why these never rely on their colour.
void WaveformCard::paintMark(juce::Graphics& g, juce::Rectangle<float> bounds) const
{
    if (decision == CandidateDecision::none) return;

    const juce::PathStrokeType stroke(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    if (decision == CandidateDecision::pass)
    {
        // A crossed-out frame, struck corner to corner. It stays clear of the frame's own rule
        // so the strike reads as a mark laid on the take, not as damage to the layout.
        auto strike = bounds.reduced(26.0f, 20.0f);
        juce::Path cross;
        cross.startNewSubPath(strike.getX(), strike.getY() + 2.0f);
        cross.lineTo(strike.getRight(), strike.getBottom() - 3.0f);
        cross.startNewSubPath(strike.getRight() - 2.0f, strike.getY());
        cross.lineTo(strike.getX() + 1.0f, strike.getBottom() - 1.0f);
        g.setColour(DivergeTheme::exploration.withAlpha(0.5f));
        g.strokePath(cross, stroke);
        return;
    }

    // Keep, favourite, and exported are all circled; the ring count says how far it went. The
    // exported arrow needs its own room, so the ring shifts left when one is coming.
    const auto leaving = decision == CandidateDecision::exported;
    auto ring = juce::Rectangle<float>(30.0f, 23.0f)
                    .withCentre({ bounds.getRight() - (leaving ? 48.0f : 34.0f),
                                  bounds.getY() + 30.0f });
    juce::Path loop;
    loop.addEllipse(ring);
    g.setColour(DivergeTheme::exploration);
    g.strokePath(loop, stroke);
    if (decision == CandidateDecision::favorite || leaving)
    {
        juce::Path second;
        second.addEllipse(ring.expanded(4.0f, 3.5f).translated(1.0f, -0.5f));
        g.strokePath(second, juce::PathStrokeType(1.8f));
    }
    if (leaving)
    {
        // An arrow off the edge of the sheet: this take left for the DAW.
        juce::Path arrow;
        const auto start = ring.getRight() + 8.0f;
        const auto tip = start + 13.0f;
        arrow.startNewSubPath(start, ring.getCentreY());
        arrow.lineTo(tip, ring.getCentreY());
        arrow.startNewSubPath(tip - 5.0f, ring.getCentreY() - 4.5f);
        arrow.lineTo(tip, ring.getCentreY());
        arrow.lineTo(tip - 5.0f, ring.getCentreY() + 4.5f);
        g.strokePath(arrow, juce::PathStrokeType(1.9f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }
}

void WaveformCard::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    const auto focused = hasKeyboardFocus(true);
    const auto active = selected || focused;
    const auto corner = DivergeTheme::radius;
    const auto passed = decision == CandidateDecision::pass;

    // The loupe: the frame you are on lifts toward paper. Focus is never spent on an accent
    // colour, which keeps the grease pencil meaning only "a person decided this".
    const auto lift = hoverMix * 0.35f + (selected ? 0.5f : 0.0f);
    g.setColour(DivergeTheme::surface.interpolatedWith(DivergeTheme::raised, juce::jmin(1.0f, lift)));
    g.fillRoundedRectangle(bounds, corner);
    g.setColour(active ? DivergeTheme::text.withAlpha(0.9f)
                       : DivergeTheme::hairline.interpolatedWith(DivergeTheme::edge, hoverMix));
    g.drawRoundedRectangle(bounds, corner, active ? 1.6f : 1.0f);

    // A struck take steps back but stays legible and stays reversible.
    const auto ink = passed ? 0.4f : 1.0f;

    auto content = getLocalBounds().reduced(15, 11);
    if (heading.isNotEmpty())
    {
        auto header = content.removeFromTop(16);
        const auto numbered = heading.containsOnly("0123456789");
        // Take numbers are measurement, so they get the monospace; everything else does not.
        g.setFont(numbered ? DivergeTheme::monoBold(DivergeTheme::Type::meta)
                           : DivergeTheme::label(DivergeTheme::Type::caps));
        g.setColour((active ? DivergeTheme::text : DivergeTheme::muted).withMultipliedAlpha(ink));
        g.drawText(numbered ? heading : heading.toUpperCase(), header,
                   juce::Justification::centredLeft, false);
        content.removeFromTop(3);
    }

    if (!audioFile.existsAsFile())
    {
        // Empty slot: a dashed frame waiting for material, with the ask stated plainly.
        juce::Path outline;
        outline.addRoundedRectangle(bounds.reduced(7.0f), corner - 6.0f);
        juce::Path dashed;
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(DivergeTheme::edge.interpolatedWith(DivergeTheme::muted, hoverMix));
        g.fillPath(dashed);

        auto empty = content;
        auto plusArea = empty.removeFromLeft(34).withSizeKeepingCentre(24, 24).toFloat();
        g.setColour(DivergeTheme::muted.interpolatedWith(DivergeTheme::text, hoverMix));
        g.fillRect(plusArea.getCentreX() - 7.0f, plusArea.getCentreY() - 0.9f, 14.0f, 1.8f);
        g.fillRect(plusArea.getCentreX() - 0.9f, plusArea.getCentreY() - 7.0f, 1.8f, 14.0f);
        empty.removeFromLeft(6);
        g.setColour(DivergeTheme::muted.brighter(hoverMix * 0.35f));
        g.setFont(DivergeTheme::body(DivergeTheme::Type::lead));
        g.drawFittedText(prompt, empty, juce::Justification::centredLeft, 2);
        return;
    }

    auto playArea = content.removeFromLeft(32).withSizeKeepingCentre(26, 26).toFloat();
    if (playing)
    {
        g.setColour(DivergeTheme::text.withMultipliedAlpha(ink));
        g.fillEllipse(playArea);
        g.setColour(DivergeTheme::canvas);
        g.fillRect(playArea.getCentreX() - 4.5f, playArea.getCentreY() - 5.0f, 3.0f, 10.0f);
        g.fillRect(playArea.getCentreX() + 1.5f, playArea.getCentreY() - 5.0f, 3.0f, 10.0f);
    }
    else
    {
        g.setColour(DivergeTheme::edge.interpolatedWith(DivergeTheme::text, hoverMix * 0.8f)
                        .withMultipliedAlpha(ink));
        g.drawEllipse(playArea.reduced(0.6f), 1.2f);
        juce::Path play;
        play.addTriangle(playArea.getCentreX() - 3.0f, playArea.getCentreY() - 5.0f,
                         playArea.getCentreX() - 3.0f, playArea.getCentreY() + 5.0f,
                         playArea.getCentreX() + 5.5f, playArea.getCentreY());
        g.setColour((hoverMix > 0.5f || selected ? DivergeTheme::text : DivergeTheme::muted)
                        .withMultipliedAlpha(ink));
        g.fillPath(play);
    }
    content.removeFromLeft(9);

    auto waveform = content;
    if (supportingText.isNotEmpty())
    {
        auto detail = waveform.removeFromBottom(16);
        g.setColour(DivergeTheme::muted.withMultipliedAlpha(ink));
        g.setFont(DivergeTheme::body(DivergeTheme::Type::meta - 0.5f));
        g.drawFittedText(supportingText, detail, juce::Justification::centredLeft, 1);
    }
    if (thumbnail.getTotalLength() > 0.0)
    {
        // The take is drawn in graphite; the part already heard is developed up to paper white.
        // The waveform is the subject of the whole surface, so it clears text contrast rather
        // than sitting back as texture.
        g.setColour((selected ? DivergeTheme::muted.brighter(0.18f) : DivergeTheme::muted)
                        .withMultipliedAlpha(ink));
        thumbnail.drawChannels(g, waveform, 0.0, thumbnail.getTotalLength(), 0.95f);
        const auto playheadX = waveform.getX()
                               + static_cast<int>(playbackProgress * waveform.getWidth());
        if (playing)
        {
            {
                juce::Graphics::ScopedSaveState clip(g);
                g.reduceClipRegion(waveform.withRight(playheadX));
                g.setColour(DivergeTheme::text);
                thumbnail.drawChannels(g, waveform, 0.0, thumbnail.getTotalLength(), 0.95f);
            }
            g.setColour(DivergeTheme::exploration);
            g.fillRect(playheadX, waveform.getY(), 1, waveform.getHeight());
        }
    }
    else
    {
        g.setColour(DivergeTheme::hairline);
        g.drawHorizontalLine(waveform.getCentreY(), static_cast<float>(waveform.getX()),
                             static_cast<float>(waveform.getRight()));
    }

    paintMark(g, getLocalBounds().toFloat());
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
