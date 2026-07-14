#pragma once

#include "WorkflowModel.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace DivergeTheme
{
// Layered darks, cool blue-black studio canvas.
inline const juce::Colour canvas { 0xff0a0d12 };
inline const juce::Colour canvasHi { 0xff0e131a };
inline const juce::Colour surface { 0xff121821 };
inline const juce::Colour raised { 0xff1a222d };
inline const juce::Colour hairline { 0xff202a35 };
inline const juce::Colour edge { 0xff2d3945 };
inline const juce::Colour text { 0xffeef4f1 };
inline const juce::Colour muted { 0xff8d9aa6 };
inline const juce::Colour dim { 0xff5d6a76 };
inline const juce::Colour exploration { 0xff4fe6c0 };
inline const juce::Colour explorationSoft { 0xff123229 };
inline const juce::Colour decision { 0xffffab5e };
inline const juce::Colour decisionSoft { 0xff33261a };
inline const juce::Colour pass { 0xff7c8790 };
inline const juce::Colour danger { 0xffff6f66 };
inline constexpr float radius = 12.0f;
inline constexpr int space = 8;

juce::Font display(float size);
juce::Font label(float size);
juce::Font body(float size);
juce::Font bodyBold(float size);
juce::Font mono(float size);
juce::Font monoBold(float size);

bool reducedMotion();
float approach(float current, float target, float rate);
void paintInnerGlow(juce::Graphics&, juce::Rectangle<float> bounds, float cornerRadius,
                    juce::Colour colour, float intensity);
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

// Click-to-dismiss dimming layer behind drawers.
class ScrimOverlay final : public juce::Component
{
public:
    std::function<void()> onDismiss;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
};

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
