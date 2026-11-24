#pragma once
#include "PluginProcessor.h"
#include <juce_dsp/juce_dsp.h>

namespace Pal
{
    inline const juce::Colour bgMain  { 0xff111c28 };
    inline const juce::Colour bgSel   { 0xff192a3c };
    inline const juce::Colour tabIdle { 0xff35506b };
    inline const juce::Colour panel   { 0xff1f324a };
    inline const juce::Colour grid    { 0xff1c2b3b };
    inline const juce::Colour axis    { 0xff5d7289 };
    inline const juce::Colour bar     { 0xff3a7ca5 };
    inline const juce::Colour yellow  { 0xffffc800 };
    inline const juce::Colour green   { 0xff8cf0a0 };
    inline const juce::Colour btnOff  { 0xff2d435d };
    inline const juce::Colour btnOn   { 0xff0f1a26 };
    inline const juce::Colour dim     { 0xff9aaabb };
}

struct MMLookAndFeel : public juce::LookAndFeel_V4
{
    MMLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
};

class MultiMeterEditor : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit MultiMeterEditor (MultiMeterProcessor&);
    ~MultiMeterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;

    void timerCallback() override;
    int  choice (const char* id) const;
    void setChoice (const char* id, int idx);
    void spectrumFor (int ch, std::vector<float>& outDb);
    void updateSpectrum (double dt);

    void paintTabs (juce::Graphics&);
    void paintAnalyzer (juce::Graphics&, juce::Rectangle<int>);
    void paintGonio (juce::Graphics&, juce::Rectangle<int>);
    void paintLevels (juce::Graphics&, juce::Rectangle<int>);
    void paintPanel (juce::Graphics&);

    float xOfFreq (double f, juce::Rectangle<float> plot) const;
    float yOfDb (float db, juce::Rectangle<float> plot) const;

    MultiMeterProcessor& proc;
    MMLookAndFeel laf;

    // Geometría
    juce::Rectangle<int> tabRect, dispRect, analyzerRect, meterRect, panelRect;
    juce::Rectangle<int> topRect, rangeRect;
    int view = 0; // 0 = Analyzer, 1 = Goniometer
    float topDb = 5.f, rangeDb = 60.f;
    float dragStartValue = 0.f;
    int dragTarget = 0;

    // Analizador
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann, false };
    std::vector<float> fftData = std::vector<float> ((size_t) fftSize * 2, 0.f);
    std::vector<float> tmpA, tmpB, bandDb, bandPeak, bandTimer, bandPow;
    double lastTime = 0.0;
    bool analyzerReset = false;

    // Controles
    juce::TextButton detBtns[4], modeBtns[3], aHoldBtn, aResetBtn, lHoldBtn, lResetBtn;
    juce::ComboBox bandsBox, aReturnBox, aPeakBox, levelBox, lReturnBox, lPeakBox;

    std::unique_ptr<APVTS::ComboBoxAttachment> bandsAtt, aReturnAtt, aPeakAtt, levelAtt, lReturnAtt, lPeakAtt;
    std::unique_ptr<APVTS::ButtonAttachment> aHoldAtt, lHoldAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MultiMeterEditor)
};
