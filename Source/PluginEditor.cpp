#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    // paleta vintage — brudny stary kompresor
    const juce::Colour kCream      { 0xffd8ccb2 };
    const juce::Colour kInk        { 0xff32291c }; // ciemny sepiowy "tusz" (czarny analizator)
    const juce::Colour kRedInk     { 0xff96271b }; // czerwony analizator wyjscia
    const juce::Colour kAmber      { 0xffe8a13c };
    const juce::Colour kBypassRed  { 0xff9c3f34 };

    void styleVintageButton (juce::TextButton& b,
                             juce::Colour onColour = kAmber,
                             juce::Colour onText   = juce::Colour (0xff241c10))
    {
        b.setColour (juce::TextButton::buttonColourId,   kCream);
        b.setColour (juce::TextButton::buttonOnColourId, onColour);
        b.setColour (juce::TextButton::textColourOffId,  kInk);
        b.setColour (juce::TextButton::textColourOnId,   onText);
    }
}

//==============================================================================
void VintageLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                               const juce::Colour&,
                                               bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const bool on = b.getToggleState();

    juce::Colour base = on ? b.findColour (juce::TextButton::buttonOnColourId)
                           : b.findColour (juce::TextButton::buttonColourId);
    if (down)
        base = base.darker (0.20f);
    else if (highlighted)
        base = base.brighter (0.06f);

    juce::ColourGradient grad (base.brighter (0.12f), r.getX(), r.getY(),
                               base.darker (0.28f),   r.getX(), r.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);

    g.setColour (juce::Colour (0xff1c150c).withAlpha (0.28f));
    g.drawRoundedRectangle (r.reduced (1.0f), 3.5f, 2.5f);

    g.setColour (juce::Colours::white.withAlpha (on ? 0.30f : 0.18f));
    g.drawLine (r.getX() + 4.0f, r.getY() + 2.0f, r.getRight() - 4.0f, r.getY() + 2.0f, 1.0f);

    g.setColour (juce::Colour (0xff14100a).withAlpha (0.85f));
    g.drawRoundedRectangle (r, 4.0f, 1.3f);
}

juce::Font VintageLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    const float s = uiScale;
    return juce::Font (juce::FontOptions ("Georgia",
                                          juce::jlimit (12.0f * s, 16.0f * s, (float) buttonHeight * 0.45f),
                                          juce::Font::bold));
}

//==============================================================================
MixCheckerEditor::MixCheckerEditor (MixCheckerProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setLookAndFeel (&vintageLnF);

    backgroundImage = juce::ImageCache::getFromMemory (BinaryData::background_png,
                                                       BinaryData::background_pngSize);

    const auto& bands = MixCheckerProcessor::getBands();

    for (int i = 0; i < MixCheckerProcessor::kNumBands; ++i)
    {
        auto& col = columns[(size_t) i];

        styleVintageButton (col.minusButton);
        styleVintageButton (col.plusButton);
        col.minusButton.onClick = [this, i] { nudgeGain (i, -MixCheckerProcessor::kGainStepDb); };
        col.plusButton.onClick  = [this, i] { nudgeGain (i,  MixCheckerProcessor::kGainStepDb); };
        addAndMakeVisible (col.minusButton);
        addAndMakeVisible (col.plusButton);

        col.gainLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (col.gainLabel);

        col.soloButton.setButtonText (bands[(size_t) i].name);
        styleVintageButton (col.soloButton);
        col.soloButton.onClick = [this, i]
        {
            const int current = (int) processor.apvts.getRawParameterValue ("band")->load();
            setBandParam (current == i ? MixCheckerProcessor::kSoloOff : i);
        };
        addAndMakeVisible (col.soloButton);

        col.rangeLabel.setText (bands[(size_t) i].rangeText, juce::dontSendNotification);
        col.rangeLabel.setJustificationType (juce::Justification::centred);
        col.rangeLabel.setColour (juce::Label::textColourId, kCream.withAlpha (0.75f));
        addAndMakeVisible (col.rangeLabel);
    }

    auto setupMsButton = [this] (juce::TextButton& b, int mode)
    {
        styleVintageButton (b);
        b.onClick = [this, mode]
        {
            if (auto* param = processor.apvts.getParameter ("msmode"))
            {
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->convertTo0to1 ((float) mode));
                param->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    };
    setupMsButton (stereoButton, 0);
    setupMsButton (midButton,    1);
    setupMsButton (sideButton,   2);

    bypassButton.setClickingTogglesState (true);
    styleVintageButton (bypassButton, kBypassRed, kCream);
    bypassButton.onClick = [this]
    {
        if (auto* param = processor.apvts.getParameter ("bypass"))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (bypassButton.getToggleState() ? 1.0f : 0.0f);
            param->endChangeGesture();
        }
    };
    addAndMakeVisible (bypassButton);

    scopeIn.fill (0.0f);
    scopeOut.fill (0.0f);

    // okno w proporcjach grafiki tla
    int w = 820, h = 460;
    if (backgroundImage.isValid())
    {
        w = backgroundImage.getWidth();
        h = backgroundImage.getHeight();
        while (w > 1200) { w /= 2; h /= 2; }
    }
    baseWidth = w;
    baseHeight = h;

    // skalowanie GUI: 60%..200%, zachowane proporcje grafiki
    setResizable (true, true);
    setResizeLimits ((int) (w * 0.6f), (int) (h * 0.6f), w * 2, h * 2);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) w / (double) h);

    setSize (w, h);

    startTimerHz (30);
    timerCallback();
}

MixCheckerEditor::~MixCheckerEditor()
{
    setLookAndFeel (nullptr);
}

//==============================================================================
void MixCheckerEditor::setBandParam (int newIndex)
{
    if (auto* param = processor.apvts.getParameter ("band"))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 ((float) newIndex));
        param->endChangeGesture();
    }
}

void MixCheckerEditor::nudgeGain (int bandIndex, float deltaDb)
{
    const auto id = MixCheckerProcessor::gainParamID (bandIndex);
    if (auto* param = processor.apvts.getParameter (id))
    {
        const float current = processor.apvts.getRawParameterValue (id)->load();
        const float next = juce::jlimit (MixCheckerProcessor::kGainMinDb,
                                         MixCheckerProcessor::kGainMaxDb,
                                         current + deltaDb);
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (next));
        param->endChangeGesture();
    }
}

//==============================================================================
void MixCheckerEditor::updateOneSpectrum (MixCheckerProcessor::AnalyserTap& tap,
                                          std::array<float, 512>& scope)
{
    if (! tap.ready.load())
    {
        for (auto& v : scope)
            v *= 0.92f;
        return;
    }

    fftData.fill (0.0f);
    std::copy (tap.block.begin(), tap.block.end(), fftData.begin());
    tap.ready.store (false);

    fftWindow.multiplyWithWindowingTable (fftData.data(), (size_t) MixCheckerProcessor::kFFTSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const auto fs = (float) processor.getSampleRate();
    if (fs <= 0.0f)
        return;

    const int halfSize = MixCheckerProcessor::kFFTSize / 2;
    const float norm = 1.0f / ((float) MixCheckerProcessor::kFFTSize * 0.25f);

    auto magAt = [this, halfSize] (float binF) -> float
    {
        const int b0 = juce::jlimit (0, halfSize - 1, (int) binF);
        const float t = juce::jlimit (0.0f, 1.0f, binF - (float) b0);
        return fftData[(size_t) b0] * (1.0f - t) + fftData[(size_t) (b0 + 1)] * t;
    };

    float prevBinF = kMinFreq / (fs * 0.5f) * (float) halfSize;

    for (int i = 0; i < kScopePoints; ++i)
    {
        const float frac = (float) i / (float) (kScopePoints - 1);
        const float freq = kMinFreq * std::pow (kMaxFreq / kMinFreq, frac);
        const float binF = juce::jlimit (0.0f, (float) halfSize,
                                         freq / (fs * 0.5f) * (float) halfSize);

        float mag = magAt (binF);
        for (int b = (int) prevBinF + 1; b <= (int) binF; ++b)
            mag = juce::jmax (mag, fftData[(size_t) juce::jlimit (0, halfSize, b)]);
        prevBinF = binF;

        float db = juce::Decibels::gainToDecibels (mag * norm, -120.0f);
        db += kTiltDbPerOct * std::log2 (freq / 1000.0f);

        const float lvl = juce::jmap (juce::jlimit (-90.0f, 0.0f, db),
                                      -90.0f, 0.0f, 0.0f, 1.0f);

        scope[(size_t) i] = juce::jmax (lvl, scope[(size_t) i] * 0.85f);
    }
}

float MixCheckerEditor::freqToX (float freqHz) const
{
    const float clamped = juce::jlimit (kMinFreq, kMaxFreq, freqHz);
    const float frac = std::log (clamped / kMinFreq) / std::log (kMaxFreq / kMinFreq);
    return (float) spectrumBounds.getX() + frac * (float) spectrumBounds.getWidth();
}

void MixCheckerEditor::drawSpectrum (juce::Graphics& g)
{
    auto r = spectrumBounds.toFloat();
    const float s = uiScale;

    // siatka + wartosci Hz
    g.setFont (juce::Font (juce::FontOptions ("Georgia", 10.0f * s, juce::Font::plain)));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = freqToX (f);
        g.setColour (kInk.withAlpha (0.12f));
        g.drawVerticalLine ((int) x, r.getY() + 4.0f * s, r.getBottom() - 16.0f * s);
        g.setColour (kInk.withAlpha (0.65f));
        const juce::String label = f >= 1000.0f ? juce::String ((int) (f / 1000.0f)) + " kHz"
                                                : juce::String ((int) f) + " Hz";
        g.drawText (label, (int) (x - 24.0f * s), (int) (r.getBottom() - 15.0f * s),
                    (int) (48.0f * s), (int) (12.0f * s), juce::Justification::centred);
    }

    // podswietlenie solowanego pasma (z migotaniem starej lampy po wlaczeniu)
    const int solo = (int) processor.apvts.getRawParameterValue ("band")->load();
    if (solo >= 0 && solo < MixCheckerProcessor::kNumBands)
    {
        const auto& band = MixCheckerProcessor::getBands()[(size_t) solo];
        const float x1 = freqToX (band.lowHz);
        const float x2 = band.highHz > 0.0f ? freqToX (band.highHz) : r.getRight();
        g.setColour (kAmber.withAlpha (0.22f * highlightAlpha));
        g.fillRect (x1, r.getY() + 1.0f, x2 - x1, r.getHeight() - 2.0f);
    }

    const float bottom = r.getBottom() - 16.0f * s;
    const float top    = r.getY() + 6.0f * s;

    auto buildPath = [&] (const std::array<float, kScopePoints>& scope)
    {
        juce::Path path;
        path.startNewSubPath (r.getX(), bottom);
        for (int i = 0; i < kScopePoints; ++i)
        {
            const float x = r.getX() + (float) i / (float) (kScopePoints - 1) * r.getWidth();
            const float y = bottom - scope[(size_t) i] * (bottom - top);
            path.lineTo (x, y);
        }
        path.lineTo (r.getRight(), bottom);
        path.closeSubPath();
        return path;
    };

    // czerwony analizator (wyjscie) — drugoplanowy, tylko gdy EQ cos zmienia
    if (outScopeVisible)
    {
        auto outPath = buildPath (scopeOut);
        g.setColour (kRedInk.withAlpha (0.18f));
        g.fillPath (outPath);
        g.setColour (kRedInk.withAlpha (0.70f));
        g.strokePath (outPath, juce::PathStrokeType (1.2f * s));
    }

    // czarny analizator (wejscie) — pierwszoplanowy
    auto inPath = buildPath (scopeIn);
    g.setColour (kInk.withAlpha (0.22f));
    g.fillPath (inPath);
    g.setColour (kInk.withAlpha (0.85f));
    g.strokePath (inPath, juce::PathStrokeType (1.5f * s));
}

//==============================================================================
void MixCheckerEditor::timerCallback()
{
    updateOneSpectrum (processor.analyserIn,  scopeIn);
    updateOneSpectrum (processor.analyserOut, scopeOut);

    // czerwony widoczny tylko gdy jakikolwiek gain != 0
    outScopeVisible = false;
    for (int i = 0; i < MixCheckerProcessor::kNumBands; ++i)
        if (processor.apvts.getRawParameterValue (MixCheckerProcessor::gainParamID (i))->load() != 0.0f)
            { outScopeVisible = true; break; }

    const int current = (int) processor.apvts.getRawParameterValue ("band")->load();

    // migotanie jak stara lampa: po wlaczeniu pasma losowe blyski,
    // coraz gestsze, po ~0.9 s stabilne swiatlo
    if (current != lastSolo)
    {
        lastSolo = current;
        if (current >= 0 && current < MixCheckerProcessor::kNumBands)
            flickerCounter = 0;
    }
    if (flickerCounter < kFlickerFrames)
    {
        const float t = (float) flickerCounter / (float) kFlickerFrames;
        const bool lit = flickerRandom.nextFloat() < (0.25f + 0.75f * t);
        highlightAlpha = lit ? (0.70f + 0.30f * flickerRandom.nextFloat()) : 0.06f;
        ++flickerCounter;
    }
    else
    {
        highlightAlpha = 1.0f;
    }

    for (int i = 0; i < MixCheckerProcessor::kNumBands; ++i)
    {
        auto& col = columns[(size_t) i];
        col.soloButton.setToggleState (i == current, juce::dontSendNotification);

        const float db = processor.apvts.getRawParameterValue (
                             MixCheckerProcessor::gainParamID (i))->load();
        juce::String text = (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
        col.gainLabel.setText (text, juce::dontSendNotification);
        col.gainLabel.setColour (juce::Label::textColourId,
                                 db == 0.0f ? kCream.withAlpha (0.55f) : kAmber);
    }

    const int msMode = (int) processor.apvts.getRawParameterValue ("msmode")->load();
    stereoButton.setToggleState (msMode == 0, juce::dontSendNotification);
    midButton   .setToggleState (msMode == 1, juce::dontSendNotification);
    sideButton  .setToggleState (msMode == 2, juce::dontSendNotification);

    const bool bypassed = processor.apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    bypassButton.setToggleState (bypassed, juce::dontSendNotification);

    repaint (spectrumBounds);
}

void MixCheckerEditor::paint (juce::Graphics& g)
{
    if (backgroundImage.isValid())
        g.drawImage (backgroundImage, getLocalBounds().toFloat());
    else
        g.fillAll (juce::Colour (0xff16181d));

    // napis "Mix Checker" w lewym górnym rogu, w stylu vintage
    const float s = uiScale;
    g.setColour (kCream.withAlpha (0.90f));
    g.setFont (juce::Font (juce::FontOptions ("Georgia", 17.0f * s, juce::Font::bold | juce::Font::italic)));
    g.drawText ("Mix Checker",
                (int) (0.033f * (float) getWidth()), (int) (0.028f * (float) getHeight()),
                (int) (0.45f * (float) getWidth()), (int) (0.065f * (float) getHeight()),
                juce::Justification::centredLeft);

    drawSpectrum (g);
}

void MixCheckerEditor::resized()
{
    const float W = (float) getWidth();
    const float H = (float) getHeight();
    uiScale = W / (float) baseWidth;
    vintageLnF.uiScale = uiScale;
    const float s = uiScale;

    // fonty etykiet skaluja sie z oknem
    const auto gainFont  = juce::Font (juce::FontOptions ("Georgia", 11.0f * s, juce::Font::bold));
    const auto rangeFont = juce::Font (juce::FontOptions ("Georgia", 11.0f * s, juce::Font::italic));

    // spectrum dopasowane do podswietlonego panelu grafiki
    // krawedzie panelu zmierzone z Assets/background.png (analiza pikseli)
    spectrumBounds = juce::Rectangle<int> ((int) (0.0452f * W), (int) (0.1284f * H),
                                           (int) (0.9206f * W), (int) (0.3198f * H));

    // kolumny pasm — przycisk solo bezposrednio pod rzedem -/+
    auto bandArea = juce::Rectangle<int> ((int) (0.030f * W), (int) (0.505f * H),
                                          (int) (0.940f * W), (int) (0.330f * H));
    const int w = bandArea.getWidth() / MixCheckerProcessor::kNumBands;

    for (auto& col : columns)
    {
        auto c = bandArea.removeFromLeft (w).reduced ((int) (3.0f * s), 0);

        auto gainRow = c.removeFromTop ((int) (24.0f * s));
        const int third = gainRow.getWidth() / 3;
        col.minusButton.setBounds (gainRow.removeFromLeft (third).reduced ((int) s));
        col.plusButton .setBounds (gainRow.removeFromRight (third).reduced ((int) s));
        col.gainLabel  .setBounds (gainRow);
        col.gainLabel  .setFont (gainFont);

        c.removeFromTop ((int) (5.0f * s));

        // solo od razu pod -/+, wysrodkowany, z marginesami
        auto soloArea = c.removeFromTop ((int) (44.0f * s))
                          .reduced ((int) (6.0f * s), 0);
        col.soloButton.setBounds (soloArea);

        c.removeFromTop ((int) (4.0f * s));
        col.rangeLabel.setBounds (c.removeFromTop ((int) (16.0f * s)));
        col.rangeLabel.setFont (rangeFont);
    }

    // dolny rzad: monitor + bypass
    auto bottom = juce::Rectangle<int> ((int) (0.030f * W), (int) (0.885f * H),
                                        (int) (0.940f * W), (int) (0.075f * H));

    auto ms = bottom.removeFromLeft ((int) (0.33f * W));
    const int msW = ms.getWidth() / 3;
    stereoButton.setBounds (ms.removeFromLeft (msW).reduced ((int) (2.0f * s), 0));
    midButton   .setBounds (ms.removeFromLeft (msW).reduced ((int) (2.0f * s), 0));
    sideButton  .setBounds (ms.removeFromLeft (msW).reduced ((int) (2.0f * s), 0));

    bypassButton.setBounds (bottom.removeFromRight ((int) (0.14f * W)));
}
