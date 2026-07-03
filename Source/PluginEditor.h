#pragma once

#include "PluginProcessor.h"

//==============================================================================
// Vintage look — przyciski jak plakietki starego kompresora
//==============================================================================
class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    float uiScale = 1.0f;

    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

//==============================================================================
class MixCheckerEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit MixCheckerEditor (MixCheckerProcessor&);
    ~MixCheckerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setBandParam (int newIndex);
    void nudgeGain (int bandIndex, float deltaDb);
    void updateOneSpectrum (MixCheckerProcessor::AnalyserTap&,
                            std::array<float, 512>& scope);
    void drawSpectrum (juce::Graphics&);
    float freqToX (float freqHz) const;

    MixCheckerProcessor& processor;
    VintageLookAndFeel vintageLnF;
    juce::Image backgroundImage;

    int baseWidth = 820, baseHeight = 460;
    float uiScale = 1.0f;

    struct BandColumn
    {
        juce::TextButton minusButton { "-" };
        juce::TextButton plusButton  { "+" };
        juce::Label      gainLabel;
        juce::TextButton soloButton;
        juce::Label      rangeLabel;
    };

    std::array<BandColumn, MixCheckerProcessor::kNumBands> columns;

    juce::TextButton stereoButton { "STEREO" };
    juce::TextButton midButton    { "MID" };
    juce::TextButton sideButton   { "SIDE" };
    juce::TextButton bypassButton { "BYPASS" };

    // Analizatory spektrum
    static constexpr int kScopePoints = 512;
    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;
    static constexpr float kTiltDbPerOct = 4.5f; // tilt wyswietlania, pivot 1 kHz

    juce::dsp::FFT fft { MixCheckerProcessor::kFFTOrder };
    juce::dsp::WindowingFunction<float> fftWindow {
        (size_t) MixCheckerProcessor::kFFTSize,
        juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 2 * MixCheckerProcessor::kFFTSize> fftData {};
    std::array<float, kScopePoints> scopeIn {};  // czarny — wejscie
    std::array<float, kScopePoints> scopeOut {}; // czerwony — wyjscie (tylko gdy EQ aktywny)
    bool outScopeVisible = false;
    juce::Rectangle<int> spectrumBounds;

    // efekt "starej lampy" przy wlaczeniu pasma
    static constexpr int kFlickerFrames = 27; // ~0.9 s przy 30 fps
    int lastSolo = -2;
    int flickerCounter = kFlickerFrames;
    float highlightAlpha = 1.0f;
    juce::Random flickerRandom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixCheckerEditor)
};
