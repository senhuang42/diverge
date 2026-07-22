#pragma once

#include "WorkflowModel.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Diverge reads as a photographic contact sheet: eight takes of one idea, laid out to be
// scanned, compared, and marked. The ground is warm darkroom graphite rather than the
// blue-black-plus-neon that plugin chrome defaults to. Colour is spent on exactly two jobs.
// Chinagraph red is the grease pencil: it appears only where a person made a decision, and
// the mark's shape carries the meaning so the state survives without colour vision. Safelight
// amber reports what the machine is doing. Focus is not a colour at all; the frame under the
// loupe simply lifts toward paper white. Everything else is neutral, which is what gives the
// two accents their force.
namespace DivergeTheme
{
// Warm graphite ramp. Hue stays around 40 degrees at very low chroma so the neutrals read as
// paper stock and darkroom board rather than as cool screen grey.
inline const juce::Colour canvas { 0xff151310 };   // deepest ground
inline const juce::Colour canvasHi { 0xff1b1814 }; // ground, lit side
inline const juce::Colour surface { 0xff201d18 };  // the sheet frames sit on
inline const juce::Colour raised { 0xff2a2620 };   // a frame cell
inline const juce::Colour hairline { 0xff322d25 }; // quiet rule
inline const juce::Colour edge { 0xff443d32 };     // stated rule
inline const juce::Colour text { 0xfff4f0e7 };     // photographic paper white
inline const juce::Colour muted { 0xffa79d8b };
inline const juce::Colour dim { 0xff6e6656 };

// The grease pencil. Rare by design: a person decided something here.
inline const juce::Colour exploration { 0xffff4a26 };
inline const juce::Colour explorationSoft { 0xff3b180e };
// Safelight. The machine is working, or wants attention.
inline const juce::Colour decision { 0xfff0a93c };
inline const juce::Colour decisionSoft { 0xff38260f };
inline const juce::Colour pass { 0xff6e6656 };
inline const juce::Colour danger { 0xffff4a26 };

// Radii stay in the 12-16px band for panels and frames; pills are for small controls only.
inline constexpr float radius = 13.0f;
// Four-unit base. An eight-only scale misses the middle steps that make tight groups tight.
inline constexpr int space = 4;

// One ramp with obvious steps, so a role is recognisable before the copy is read.
namespace Type
{
inline constexpr float display = 26.0f;
inline constexpr float title = 17.0f;
inline constexpr float lead = 15.0f;
inline constexpr float body = 13.5f;
inline constexpr float meta = 12.0f;
inline constexpr float caps = 10.5f;
}

juce::Font display(float size);
juce::Font title(float size);
juce::Font label(float size);
juce::Font body(float size);
juce::Font bodyBold(float size);
juce::Font mono(float size);
juce::Font monoBold(float size);

bool reducedMotion();
float approach(float current, float target, float rate);

// Real depth: an offset and a soft blur. Used only where something genuinely floats above
// the sheet, and never on an element that also states a border.
void paintDropShadow(juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius,
                     float blur, float offsetY, float opacity);
// Crisp keyboard focus ring. Not a glow: it has no blur and it reads at a glance.
void paintFocusRing(juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius);
}

class DivergeLookAndFeel final : public juce::LookAndFeel_V4, private juce::Timer
{
public:
    DivergeLookAndFeel();
    ~DivergeLookAndFeel() override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosition, float minSliderPosition, float maxSliderPosition,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawCornerResizer(juce::Graphics&, int width, int height, bool mouseOver, bool dragging) override;
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;

private:
    float animatedMix(juce::Component&, float target);
    void timerCallback() override;

    struct HoverState
    {
        juce::Component::SafePointer<juce::Component> component;
        float value = 0.0f;
        float target = 0.0f;
    };
    std::vector<HoverState> hoverStates;
};

// Bottom-centre notification pill that slides up, holds, and fades on its own.
class ToastOverlay final : public juce::Component, private juce::Timer
{
public:
    ToastOverlay();
    void show(juce::String message);

private:
    void paint(juce::Graphics&) override;
    void timerCallback() override;

    juce::String text;
    int ticksLeft = 0;
    float slide = 0.0f;
};

// Rounded elevated panel used for drawers and full-view panels.
class PanelSurface final : public juce::Component
{
public:
    void paint(juce::Graphics&) override;
};

// Small card showing one subsystem's health: title, state dot, body copy.
class StatusCard final : public juce::Component
{
public:
    enum class State { neutral, ok, attention };
    void set(juce::String title, juce::String body, State state);
    void paint(juce::Graphics&) override;

private:
    juce::String title;
    juce::String body;
    State state = State::neutral;
};

// Click-to-dismiss dimming layer behind drawers.
class ScrimOverlay final : public juce::Component
{
public:
    std::function<void()> onDismiss;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
};

// One frame on the contact sheet. Carries its own take number, waveform, transport, and the
// grease-pencil mark a decision leaves on it.
class WaveformCard final : public juce::Component
{
public:
    WaveformCard(juce::AudioFormatManager&, juce::AudioThumbnailCache&);

    std::function<void()> onActivate;
    std::function<void()> onChoose;
    std::function<void()> onDrag;
    std::function<void(double)> onSeek;

    void setAudio(juce::String title, juce::String emptyPrompt, const juce::File&);
    void setSupportingText(juce::String text);
    void setState(bool selected, bool playing, double progress, CandidateDecision decision);
    void setDraggable(bool shouldDrag) noexcept { draggable = shouldDrag; }
    void beginEntrance(int order);
    const juce::File& file() const noexcept { return audioFile; }
    void advanceAnimation();

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    void paintMark(juce::Graphics&, juce::Rectangle<float> bounds) const;

    juce::String heading;
    juce::String prompt;
    juce::String supportingText;
    juce::File audioFile;
    juce::AudioThumbnail thumbnail;
    bool selected = false;
    bool playing = false;
    bool hovered = false;
    bool draggable = false;
    double playbackProgress = 0.0;
    float hoverMix = 0.0f;
    float entrance = 1.0f;
    int entranceDelay = 0;
    CandidateDecision decision = CandidateDecision::none;
};
