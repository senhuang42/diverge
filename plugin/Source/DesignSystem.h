#pragma once

#include "WorkflowModel.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace DivergeTheme
{
inline const juce::Colour canvas { 0xff090b0d };
inline const juce::Colour surface { 0xff111519 };
inline const juce::Colour raised { 0xff181e23 };
inline const juce::Colour edge { 0xff293139 };
inline const juce::Colour text { 0xfff1f4f2 };
inline const juce::Colour muted { 0xff8e9a9f };
inline const juce::Colour exploration { 0xff58e6c2 };
inline const juce::Colour explorationSoft { 0xff183b34 };
inline const juce::Colour decision { 0xffffa85c };
inline const juce::Colour pass { 0xff7c8790 };
inline const juce::Colour danger { 0xffff6f66 };
inline constexpr float radius = 10.0f;
inline constexpr int space = 8;
}

class DivergeLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    DivergeLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&,
                              bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosition, float minSliderPosition, float maxSliderPosition,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
};

class WaveformCard final : public juce::Component
{
public:
    WaveformCard(juce::AudioFormatManager&, juce::AudioThumbnailCache&);

    std::function<void()> onActivate;
    std::function<void()> onChoose;
    std::function<void()> onDrag;

    void setAudio(juce::String title, juce::String emptyPrompt, const juce::File&);
    void setSupportingText(juce::String text);
    void setState(bool selected, bool playing, double progress, CandidateDecision decision);
    void setDraggable(bool shouldDrag) noexcept { draggable = shouldDrag; }
    const juce::File& file() const noexcept { return audioFile; }
    void advanceAnimation();

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

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
    CandidateDecision decision = CandidateDecision::none;
};
