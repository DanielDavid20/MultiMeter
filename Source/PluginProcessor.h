#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <cmath>

// Filtro biquad (forma transpuesta II) para el K-weighting de LUFS
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0; }
};

class MultiMeterProcessor : public juce::AudioProcessor
{
public:
    static constexpr int ringSize = 16384;   // potencia de 2
    static constexpr int histBins = 1000;    // histograma de LU-I: 0.1 LU por bin
    static constexpr float returnRates[6] = { 2.9f, 5.9f, 11.8f, 23.5f, 47.0f, 94.0f };
    static constexpr float holdTimes[5]   = { 1.f, 2.f, 4.f, 8.f, 16.f };
    static constexpr int   bandCounts[3]  = { 31, 63, 125 };

    MultiMeterProcessor();
    ~MultiMeterProcessor() override = default;

    void prepareToPlay (double sr, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MultiMeter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    // Datos compartidos con la interfaz (hilo de audio -> hilo de UI)
    std::array<float, ringSize> ringL {}, ringR {};
    std::atomic<int> writePos { 0 };
    std::atomic<double> sampleRate { 44100.0 };

    struct ChannelMeter
    {
        std::atomic<float> peak { -100.f }, rms { -100.f }, hold { -100.f };
    };
    ChannelMeter meters[2];

    std::atomic<float> lufsMomentary { -100.f }, lufsShort { -100.f }, lufsIntegrated { -100.f };
    std::atomic<float> correlation { 0.f };
    std::atomic<bool> resetLevel { false }, resetLoudness { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void endSubBlock();
    void resetLoudnessState();

    std::atomic<float>* pLReturn = nullptr;
    std::atomic<float>* pLPeak = nullptr;
    std::atomic<float>* pLHold = nullptr;

    // Medidor de nivel
    double rmsState[2] = { 0, 0 };
    double rmsCoef = 0.0001;
    float holdVal[2] = { -100.f, -100.f };
    double holdLeft[2] = { 0, 0 };

    // Correlación
    double sLL = 0, sRR = 0, sLR = 0, corrCoef = 0.999;

    // LUFS (ITU-R BS.1770-4)
    Biquad kShelf[2], kHP[2];
    double subEnergy = 0;
    int subCount = 0, subLen = 4410, subIdx = 0, subFilled = 0;
    std::array<double, 30> subHist {};
    std::array<double, histBins> histEnergy {};
    std::array<long, histBins> histCount {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiMeterProcessor)
};
