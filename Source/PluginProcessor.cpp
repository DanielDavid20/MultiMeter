#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace juce;

juce::AudioProcessorValueTreeState::ParameterLayout MultiMeterProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    const StringArray rates { "2.9 dB/s", "5.9 dB/s", "11.8 dB/s", "23.5 dB/s", "47.0 dB/s", "94.0 dB/s" };
    const StringArray peaks { "1 s", "2 s", "4 s", "8 s", "16 s" };

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "detection", 1 }, "Detection",
                                                        StringArray { "Left", "Right", "LRmax", "Mono" }, 3));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                        StringArray { "RMS Slow", "RMS Fast", "Peak" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "bands", 1 }, "Analyzer Bands",
                                                        StringArray { "31", "63", "125" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "aReturn", 1 }, "Analyzer Return Rate", rates, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "aPeak", 1 }, "Analyzer Peak", peaks, 2));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "aHold", 1 }, "Analyzer Hold", false));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "level", 1 }, "Level",
                                                        StringArray { "Peak & RMS", "Peak", "RMS" }, 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lReturn", 1 }, "Level Return Rate", rates, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "lPeak", 1 }, "Level Peak", peaks, 1));
    layout.add (std::make_unique<AudioParameterBool>   (ParameterID { "lHold", 1 }, "Level Hold", true));
    return layout;
}

MultiMeterProcessor::MultiMeterProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    pLReturn = apvts.getRawParameterValue ("lReturn");
    pLPeak   = apvts.getRawParameterValue ("lPeak");
    pLHold   = apvts.getRawParameterValue ("lHold");
}

bool MultiMeterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet();
    const auto out = l.getMainOutputChannelSet();
    if (in != out) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono();
}

void MultiMeterProcessor::resetLoudnessState()
{
    histEnergy.fill (0.0);
    histCount.fill (0);
    lufsIntegrated.store (-100.f);
}

void MultiMeterProcessor::prepareToPlay (double sr, int)
{
    sampleRate.store (sr);

    // Etapa 1: high shelf (pre-filtro BS.1770)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (MathConstants<double>::pi * f0 / sr);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = (Vh + Vb * K / Q + K * K) / a0;
        b.b1 = 2.0 * (K * K - Vh) / a0;
        b.b2 = (Vh - Vb * K / Q + K * K) / a0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        kShelf[0] = kShelf[1] = b;
    }
    // Etapa 2: high-pass RLB
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (MathConstants<double>::pi * f0 / sr);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;
        b.a2 = (1.0 - K / Q + K * K) / a0;
        kHP[0] = kHP[1] = b;
    }

    subLen = jmax (1, (int) std::lround (0.1 * sr));
    subEnergy = 0; subCount = 0; subIdx = 0; subFilled = 0;
    subHist.fill (0.0);
    resetLoudnessState();

    rmsCoef  = 1.0 - std::exp (-1.0 / (0.3 * sr));
    corrCoef = std::exp (-1.0 / (0.3 * sr));
    rmsState[0] = rmsState[1] = 0;
    sLL = sRR = sLR = 0;
    holdVal[0] = holdVal[1] = -100.f;
    holdLeft[0] = holdLeft[1] = 0;
}

void MultiMeterProcessor::endSubBlock()
{
    const double e = subEnergy / (double) jmax (1, subCount);
    subEnergy = 0; subCount = 0;

    subHist[(size_t) subIdx] = e;
    subIdx = (subIdx + 1) % 30;
    if (subFilled < 30) ++subFilled;

    auto avg = [this] (int count)
    {
        double s = 0;
        for (int k = 0; k < count; ++k)
            s += subHist[(size_t) ((subIdx + 29 - k) % 30)];
        return s / (double) count;
    };
    auto toLUFS = [] (double z) { return z > 1e-10 ? -0.691 + 10.0 * std::log10 (z) : -100.0; };

    lufsShort.store ((float) toLUFS (avg (subFilled)));

    if (subFilled < 4)
    {
        lufsMomentary.store (-100.f);
        return;
    }

    const double z = avg (4);              // bloque de 400 ms, solape del 75 %
    const double lm = toLUFS (z);
    lufsMomentary.store ((float) lm);

    // Integrado con doble gating (-70 LUFS absoluto, -10 LU relativo)
    if (lm > -70.0)
    {
        const int b = jlimit (0, histBins - 1, (int) ((lm + 70.0) * 10.0));
        histCount[(size_t) b]++;
        histEnergy[(size_t) b] += z;
    }

    double sumE = 0; long cnt = 0;
    for (int b = 0; b < histBins; ++b) { sumE += histEnergy[(size_t) b]; cnt += histCount[(size_t) b]; }
    if (cnt > 0)
    {
        const double thr = toLUFS (sumE / (double) cnt) - 10.0;
        const int bmin = jlimit (0, histBins - 1, (int) std::ceil ((thr + 70.0) * 10.0));
        double s2 = 0; long c2 = 0;
        for (int b = bmin; b < histBins; ++b) { s2 += histEnergy[(size_t) b]; c2 += histCount[(size_t) b]; }
        if (c2 > 0) lufsIntegrated.store ((float) toLUFS (s2 / (double) c2));
    }
}

void MultiMeterProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 1 || n == 0) return;

    if (resetLevel.exchange (false))
    {
        holdVal[0] = holdVal[1] = -100.f;
        holdLeft[0] = holdLeft[1] = 0;
        meters[0].hold.store (-100.f);
        meters[1].hold.store (-100.f);
    }
    if (resetLoudness.exchange (false))
        resetLoudnessState();

    const float* l = buffer.getReadPointer (0);
    const float* r = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : l;

    float blockPeak[2] = { 0.f, 0.f };
    int wp = writePos.load (std::memory_order_relaxed);

    for (int i = 0; i < n; ++i)
    {
        const float xl = l[i], xr = r[i];

        ringL[(size_t) wp] = xl;
        ringR[(size_t) wp] = xr;
        wp = (wp + 1) & (ringSize - 1);

        blockPeak[0] = jmax (blockPeak[0], std::abs (xl));
        blockPeak[1] = jmax (blockPeak[1], std::abs (xr));

        rmsState[0] += rmsCoef * ((double) xl * xl - rmsState[0]);
        rmsState[1] += rmsCoef * ((double) xr * xr - rmsState[1]);

        sLL = corrCoef * sLL + (1.0 - corrCoef) * (double) xl * xl;
        sRR = corrCoef * sRR + (1.0 - corrCoef) * (double) xr * xr;
        sLR = corrCoef * sLR + (1.0 - corrCoef) * (double) xl * xr;

        const double kl = kHP[0].process (kShelf[0].process (xl));
        const double kr = kHP[1].process (kShelf[1].process (xr));
        subEnergy += kl * kl + kr * kr;
        if (++subCount >= subLen)
            endSubBlock();
    }
    writePos.store (wp, std::memory_order_release);

    // Correlación
    const double den = sLL * sRR;
    correlation.store (den > 1e-18 ? (float) jlimit (-1.0, 1.0, sLR / std::sqrt (den)) : 0.f);

    // Niveles
    const double fs = sampleRate.load();
    const float dt = (float) ((double) n / fs);
    const float rate = returnRates[jlimit (0, 5, (int) std::lround (pLReturn->load()))];
    const double holdSamples = holdTimes[jlimit (0, 4, (int) std::lround (pLPeak->load()))] * fs;
    const bool holdOn = pLHold->load() > 0.5f;

    for (int c = 0; c < 2; ++c)
    {
        const float bp = Decibels::gainToDecibels (blockPeak[c], -100.f);
        float p = jmax (bp, meters[c].peak.load() - rate * dt);
        meters[c].peak.store (jmax (p, -100.f));
        meters[c].rms.store ((float) (10.0 * std::log10 (rmsState[c] + 1e-12)));

        if (bp >= holdVal[c])
        {
            holdVal[c] = bp;
            holdLeft[c] = holdSamples;
        }
        else if (! holdOn)
        {
            holdLeft[c] -= n;
            if (holdLeft[c] <= 0)
                holdVal[c] = jmax (p, holdVal[c] - rate * dt);
        }
        meters[c].hold.store (jmax (holdVal[c], -100.f));
    }
}

juce::AudioProcessorEditor* MultiMeterProcessor::createEditor() { return new MultiMeterEditor (*this); }

void MultiMeterProcessor::getStateInformation (MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void MultiMeterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MultiMeterProcessor(); }
