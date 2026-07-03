#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
MixCheckerProcessor::MixCheckerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    bandParam   = apvts.getRawParameterValue ("band");
    bypassParam = apvts.getRawParameterValue ("bypass");
    msParam     = apvts.getRawParameterValue ("msmode");

    apvts.addParameterListener ("band", this);
    for (int i = 0; i < kNumBands; ++i)
    {
        gainParams[(size_t) i] = apvts.getRawParameterValue (gainParamID (i));
        apvts.addParameterListener (gainParamID (i), this);
    }
}

MixCheckerProcessor::~MixCheckerProcessor()
{
    apvts.removeParameterListener ("band", this);
    for (int i = 0; i < kNumBands; ++i)
        apvts.removeParameterListener (gainParamID (i), this);
    cancelPendingUpdate();
}

juce::AudioProcessorValueTreeState::ParameterLayout MixCheckerProcessor::createLayout()
{
    juce::StringArray bandChoices;
    for (auto& b : getBands())
        bandChoices.add (b.name);
    bandChoices.add ("Off"); // indeks kSoloOff — cale spektrum

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterChoice> ("band", "Solo Band",
                                                              bandChoices, kSoloOff));
    layout.add (std::make_unique<juce::AudioParameterBool> ("bypass", "Bypass", false));
    layout.add (std::make_unique<juce::AudioParameterChoice> ("msmode", "Monitor",
                                                              juce::StringArray { "Stereo", "Mid", "Side" }, 0));

    for (int i = 0; i < kNumBands; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            gainParamID (i),
            juce::String (getBands()[(size_t) i].name) + " Gain",
            juce::NormalisableRange<float> (kGainMinDb, kGainMaxDb, kGainStepDb),
            0.0f));

    return layout;
}

//==============================================================================
void MixCheckerProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    baseSampleRate = sampleRate;
    const auto numChannels = (juce::uint32) juce::jmax (getMainBusNumInputChannels(),
                                                        getMainBusNumOutputChannels());
    const int factor = 1 << kOversamplingOrder; // 8
    const double osRate = sampleRate * factor;

    // Oversampling x8: filtry pólpasmowe FIR equiripple => faza liniowa,
    // useIntegerLatency = true, zeby latencja byla dokladna.
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        numChannels, kOversamplingOrder,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true, true);
    oversampling->initProcessing ((size_t) samplesPerBlock);

    // Dlugosc FIR: ~150 ms sygnalu przy fs oversamplowanym daje waskie
    // zbocza nawet dla pasma 16-60 Hz. Zaokraglona do 16k+1, zeby
    // opóznienie grupowe bylo calkowite po decymacji /8.
    int taps = (int) std::round (osRate * 0.15);
    taps = juce::jlimit (8193, 131073, taps);
    taps = ((taps - 1) / 16) * 16 + 1; // N = 16k + 1  =>  (N-1)/2 podzielne przez 8
    irNumTaps = taps;

    juce::dsp::ProcessSpec osSpec { osRate,
                                    (juce::uint32) samplesPerBlock * (juce::uint32) factor,
                                    numChannels };
    convolution.prepare (osSpec);

    juce::dsp::ProcessSpec baseSpec { sampleRate, (juce::uint32) samplesPerBlock, numChannels };
    dryWet.prepare (baseSpec);

    const int irLatencyBase    = ((irNumTaps - 1) / 2) / factor;
    const int totalLatencyBase = (int) std::ceil (oversampling->getLatencyInSamples()) + irLatencyBase;
    setLatencySamples (totalLatencyBase);
    dryWet.setWetLatency ((float) totalLatencyBase);

    rebuildAndLoadIR();
    prepared = true;
}

void MixCheckerProcessor::releaseResources()
{
    prepared = false;
    oversampling.reset();
}

bool MixCheckerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

//==============================================================================
// Kompozytowy FIR (windowed-sinc, okno Kaisera, symetryczny => liniowa faza):
//
// - Solo aktywne: pasmowoprzepustowy w oryginalnym zakresie pasma,
//   przeskalowany przez gain tego pasma.
// - Solo Off: 8-pasmowy korektor zbudowany z komplementarnych crossoverów.
//   Czestotliwosci podzialu leza w srodku geometrycznym nakladek pasm.
//   h_eq = suma_k g_k * (lowpass(fc_{k+1}) - lowpass(fc_k))  — suma telescopowa,
//   wiec przy wszystkich gainach 0 dB odpowiedz jest DOKLADNIE plaska (delta).
//==============================================================================
juce::AudioBuffer<float> MixCheckerProcessor::designCompositeIR() const
{
    const double osRate = baseSampleRate * (1 << kOversamplingOrder);
    const int N = irNumTaps;
    const int M = (N - 1) / 2;
    const auto& bands = getBands();

    // sinc dolnoprzepustowy o odcieciu f (znormalizowanym), x = n - M
    auto lp = [] (double f, double x) -> double
    {
        if (x == 0.0)
            return 2.0 * f;
        return std::sin (juce::MathConstants<double>::twoPi * f * x)
             / (juce::MathConstants<double>::pi * x);
    };

    const int solo = (int) bandParam->load();

    juce::AudioBuffer<float> ir (1, N);
    auto* h = ir.getWritePointer (0);

    if (solo >= 0 && solo < kNumBands)
    {
        const auto& band = bands[(size_t) solo];
        const double f1 = juce::jlimit (0.0, 0.5, band.lowHz / osRate);
        const double f2 = band.highHz > 0.0f
                              ? juce::jlimit (0.0, 0.5, (double) band.highHz / osRate)
                              : 0.5;
        const double g = juce::Decibels::decibelsToGain ((double) gainParams[(size_t) solo]->load());

        for (int n = 0; n < N; ++n)
        {
            const double x = (double) (n - M);
            h[n] = (float) (g * (lp (f2, x) - lp (f1, x)));
        }
    }
    else
    {
        // Crossovery: 0, sqrt(hi_{k-1} * lo_k), ..., Nyquist
        std::array<double, kNumBands + 1> fc {};
        fc[0] = 0.0;
        for (int k = 1; k < kNumBands; ++k)
            fc[(size_t) k] = std::sqrt ((double) bands[(size_t)(k - 1)].highHz
                                        * (double) bands[(size_t) k].lowHz) / osRate;
        fc[kNumBands] = 0.5;

        std::array<double, kNumBands> g {};
        for (int k = 0; k < kNumBands; ++k)
            g[(size_t) k] = juce::Decibels::decibelsToGain ((double) gainParams[(size_t) k]->load());

        for (int n = 0; n < N; ++n)
        {
            const double x = (double) (n - M);
            double acc = 0.0;
            for (int k = 0; k < kNumBands; ++k)
                acc += g[(size_t) k] * (lp (fc[(size_t)(k + 1)], x) - lp (fc[(size_t) k], x));
            h[n] = (float) acc;
        }
    }

    juce::dsp::WindowingFunction<float> window ((size_t) N,
                                                juce::dsp::WindowingFunction<float>::kaiser,
                                                false, 9.0f);
    window.multiplyWithWindowingTable (h, (size_t) N);
    return ir;
}

void MixCheckerProcessor::rebuildAndLoadIR()
{
    if (irNumTaps <= 0)
        return;

    const double osRate = baseSampleRate * (1 << kOversamplingOrder);
    convolution.loadImpulseResponse (designCompositeIR(), osRate,
                                     juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);
}

void MixCheckerProcessor::parameterChanged (const juce::String&, float)
{
    triggerAsyncUpdate(); // przebudowa IR poza watkiem audio (skoalescowana)
}

void MixCheckerProcessor::handleAsyncUpdate()
{
    if (prepared)
        rebuildAndLoadIR();
}

//==============================================================================
void MixCheckerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (! prepared || oversampling == nullptr)
        return;

    for (int ch = getMainBusNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const bool bypassed = bypassParam->load() > 0.5f;
    const int  msMode   = (int) msParam->load(); // 0 Stereo, 1 Mid, 2 Side

    feedAnalyser (analyserIn, buffer); // czarny analizator: wejscie wtyczki

    juce::dsp::AudioBlock<float> block (buffer);

    // Dry/wet o stalej latencji: bypass = 100% dry (opózniony), dzieki czemu
    // przelaczanie nie zmienia latencji raportowanej hostowi.
    dryWet.setWetMixProportion (bypassed ? 0.0f : 1.0f);
    dryWet.pushDrySamples (block);

    // Monitoring Mid/Side (tylko tor odsluchu; bypass = oryginalne stereo)
    if (msMode != 0 && buffer.getNumChannels() >= 2)
    {
        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float mid  = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]);
            const float v = (msMode == 1) ? mid : side;
            l[i] = v;
            r[i] = v;
        }
    }

    auto osBlock = oversampling->processSamplesUp (block);
    juce::dsp::ProcessContextReplacing<float> ctx (osBlock);
    convolution.process (ctx);
    oversampling->processSamplesDown (block);

    dryWet.mixWetSamples (block);

    feedAnalyser (analyserOut, buffer); // czerwony analizator: wyjscie wtyczki
}

void MixCheckerProcessor::feedAnalyser (AnalyserTap& tap, const juce::AudioBuffer<float>& buffer)
{
    const int numCh = juce::jmax (1, buffer.getNumChannels());
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        float s = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            s += buffer.getReadPointer (ch)[i];
        tap.push (s / (float) numCh);
    }
}

//==============================================================================
void MixCheckerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MixCheckerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
        triggerAsyncUpdate();
    }
}

juce::AudioProcessorEditor* MixCheckerProcessor::createEditor()
{
    return new MixCheckerEditor (*this);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MixCheckerProcessor();
}
