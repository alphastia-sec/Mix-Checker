#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

//==============================================================================
// Mix Checker — Alphastudio
// 1) Odsluch (solo) jednego z 8 pasm albo cale spektrum (solo Off).
// 2) Staly 8-pasmowy korektor liniowofazowy (kroki 2 dB) — dziala zawsze.
// 3) Monitoring Stereo / Mid / Side.
// Wszystko jako jeden kompozytowy FIR (windowed-sinc, splot FFT) w domenie
// oversamplowanej x8 (filtry pólpasmowe FIR => caly tor linear phase).
// Stala, raportowana latencja.
//==============================================================================
class MixCheckerProcessor : public juce::AudioProcessor,
                            private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
{
public:
    static constexpr int kNumBands = 8;
    static constexpr int kSoloOff  = kNumBands; // indeks "Off" w parametrze band
    static constexpr size_t kOversamplingOrder = 3; // 2^3 = x8

    static constexpr float kGainMinDb  = -12.0f;
    static constexpr float kGainMaxDb  =  12.0f;
    static constexpr float kGainStepDb =   0.5f;

    // Analizator spektrum (sygnal wyjsciowy wtyczki)
    static constexpr int kFFTOrder = 13;
    static constexpr int kFFTSize  = 1 << kFFTOrder; // 8192 — wysoka rozdzielczosc w basie

    struct BandDef
    {
        const char* name;      // nazwa na przycisku
        const char* rangeText; // zakres pod przyciskiem
        float lowHz;
        float highHz;          // <= 0 oznacza: az do Nyquista
    };

    static const std::array<BandDef, kNumBands>& getBands()
    {
        static const std::array<BandDef, kNumBands> bands = { {
            { "Low End",  "16 - 60 Hz",     16.0f,   60.0f },
            { "Bass",     "50 - 220 Hz",    50.0f,   220.0f },
            { "Low Mid",  "185 - 460 Hz",   185.0f,  460.0f },
            { "Mid",      "430 - 1000 Hz",  430.0f,  1000.0f },
            { "High Mid", "950 - 2000 Hz",  950.0f,  2000.0f },
            { "Low High", "2000 - 4300 Hz", 2000.0f, 4300.0f },
            { "High",     "3800 - 6800 Hz", 3800.0f, 6800.0f },
            { "High End", "6800+ Hz",       6800.0f, -1.0f },
        } };
        return bands;
    }

    static juce::String gainParamID (int bandIndex)
    {
        return "gain" + juce::String (bandIndex);
    }

    //==============================================================================
    MixCheckerProcessor();
    ~MixCheckerProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mix Checker"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Analizatory dla edytora (bloki mono): wejscie (czarny) i wyjscie (czerwony).
    struct AnalyserTap
    {
        std::atomic<bool> ready { false };
        std::array<float, kFFTSize> block {};

        void push (float sample)
        {
            if (index == kFFTSize)
            {
                if (! ready.load())
                {
                    block = fifo;
                    ready.store (true);
                }
                // 75% overlap => szybkie odswiezanie mimo duzego FFT
                std::copy (fifo.begin() + kFFTSize / 4, fifo.end(), fifo.begin());
                index = kFFTSize - kFFTSize / 4;
            }
            fifo[(size_t) index++] = sample;
        }

    private:
        std::array<float, kFFTSize> fifo {};
        int index = 0;
    };

    AnalyserTap analyserIn;  // sygnal wchodzacy do wtyczki
    AnalyserTap analyserOut; // sygnal wychodzacy z wtyczki

private:
    static void feedAnalyser (AnalyserTap&, const juce::AudioBuffer<float>&);

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    void rebuildAndLoadIR();
    juce::AudioBuffer<float> designCompositeIR() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    juce::dsp::Convolution convolution;
    juce::dsp::DryWetMixer<float> dryWet;

    double baseSampleRate = 44100.0;
    int irNumTaps = 0;
    bool prepared = false;

    std::atomic<float>* bandParam   = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* msParam     = nullptr;
    std::array<std::atomic<float>*, kNumBands> gainParams {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixCheckerProcessor)
};
