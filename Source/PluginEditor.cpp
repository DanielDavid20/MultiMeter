#include "PluginEditor.h"

using namespace juce;

//==============================================================================
// Utilidades
//==============================================================================
static const float dbTab[15] = { 6, 3, 0, -3, -6, -9, -12, -16, -20, -24, -30, -36, -40, -50, -60 };
static const float frTab[15] = { 0.f, .080f, .157f, .234f, .311f, .388f, .465f, .545f, .615f, .683f, .763f, .831f, .868f, .935f, 1.f };

// Escala no lineal del medidor de nivel (igual que la de Logic)
static float dbToFrac (float db)
{
    if (db >= 6.f)   return 0.f;
    if (db <= -60.f) return 1.f;
    for (int i = 0; i < 14; ++i)
        if (db <= dbTab[i] && db >= dbTab[i + 1])
        {
            const float t = (dbTab[i] - db) / (dbTab[i] - dbTab[i + 1]);
            return frTab[i] + t * (frTab[i + 1] - frTab[i]);
        }
    return 1.f;
}

static String fmtDb (float v) { return v <= -99.f ? String ("-inf") : String (v, 1); }

static double bandFHigh (double sr) { return jmin (20700.0, 0.46 * sr); }
static constexpr double bandFLow = 31.0;

// Convierte coordenadas de la captura original (1226 px de ancho, panel desde y=700, alto 208)
static Rectangle<int> panelR (Rectangle<int> panel, int w, float x0, float y0, float x1, float y1)
{
    const float sx = (float) w / 1226.f;
    const float sy = (float) panel.getHeight() / 208.f;
    return { roundToInt (x0 * sx), panel.getY() + roundToInt ((y0 - 700.f) * sy),
             roundToInt ((x1 - x0) * sx), roundToInt ((y1 - y0) * sy) };
}

//==============================================================================
// LookAndFeel
//==============================================================================
MMLookAndFeel::MMLookAndFeel()
{
    setColour (PopupMenu::backgroundColourId, Pal::panel);
    setColour (PopupMenu::textColourId, Pal::dim);
    setColour (PopupMenu::highlightedBackgroundColourId, Pal::btnOn);
    setColour (PopupMenu::highlightedTextColourId, Pal::green);
}

void MMLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (b.getToggleState() ? Pal::btnOn : Pal::btnOff.brighter (over ? 0.12f : 0.f));
    g.fillRoundedRectangle (r, 3.f);
}

void MMLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? Pal::green : Pal::dim);
    g.setFont (Font (FontOptions ((float) b.getHeight() * 0.5f)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), Justification::centred, false);
}

void MMLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox&)
{
    const float cx = (float) w - (float) h * 0.45f, cy = (float) h * 0.5f, s = (float) h * 0.14f;
    Path p;
    p.addTriangle (cx - s, cy - s * 0.6f, cx + s, cy - s * 0.6f, cx, cy - s * 2.0f);
    p.addTriangle (cx - s, cy + s * 0.6f, cx + s, cy + s * 0.6f, cx, cy + s * 2.0f);
    g.setColour (Pal::green);
    g.fillPath (p);
}

Font MMLookAndFeel::getComboBoxFont (ComboBox& box)
{
    return Font (FontOptions ((float) box.getHeight() * 0.62f));
}

//==============================================================================
// Editor
//==============================================================================
MultiMeterEditor::MultiMeterEditor (MultiMeterProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    const char* detNames[4]  = { "Left", "Right", "LRmax", "Mono" };
    const char* modeNames[3] = { "RMS Slow", "RMS Fast", "Peak" };

    for (int i = 0; i < 4; ++i)
    {
        auto& b = detBtns[i];
        b.setButtonText (detNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (101);
        b.onClick = [this, i] { setChoice ("detection", i); };
        addAndMakeVisible (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeBtns[i];
        b.setButtonText (modeNames[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (102);
        b.onClick = [this, i] { setChoice ("mode", i); };
        addAndMakeVisible (b);
    }

    auto setupCombo = [this] (ComboBox& box, const char* id, std::unique_ptr<APVTS::ComboBoxAttachment>& att)
    {
        if (auto* cp = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id)))
            box.addItemList (cp->choices, 1);
        box.setJustificationType (Justification::centred);
        box.setColour (ComboBox::textColourId, Pal::green);
        box.setColour (ComboBox::backgroundColourId, Colours::transparentBlack);
        box.setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        box.setColour (ComboBox::arrowColourId, Pal::green);
        addAndMakeVisible (box);
        att = std::make_unique<APVTS::ComboBoxAttachment> (proc.apvts, id, box);
    };
    setupCombo (bandsBox,   "bands",   bandsAtt);
    setupCombo (aReturnBox, "aReturn", aReturnAtt);
    setupCombo (aPeakBox,   "aPeak",   aPeakAtt);
    setupCombo (levelBox,   "level",   levelAtt);
    setupCombo (lReturnBox, "lReturn", lReturnAtt);
    setupCombo (lPeakBox,   "lPeak",   lPeakAtt);

    aHoldBtn.setButtonText ("Hold");  addAndMakeVisible (aHoldBtn);
    lHoldBtn.setButtonText ("Hold");  addAndMakeVisible (lHoldBtn);
    aResetBtn.setButtonText ("Reset"); addAndMakeVisible (aResetBtn);
    lResetBtn.setButtonText ("Reset"); addAndMakeVisible (lResetBtn);
    aHoldAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "aHold", aHoldBtn);
    lHoldAtt = std::make_unique<APVTS::ButtonAttachment> (proc.apvts, "lHold", lHoldBtn);
    aResetBtn.onClick = [this] { analyzerReset = true; };
    lResetBtn.onClick = [this] { proc.resetLevel = true; };

    setSize (1000, 653);
    setResizable (true, true);
    setResizeLimits (640, 418, 2000, 1306);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio (1000.0 / 653.0);

    startTimerHz (30);
}

MultiMeterEditor::~MultiMeterEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

int MultiMeterEditor::choice (const char* id) const
{
    return (int) std::lround (proc.apvts.getRawParameterValue (id)->load());
}

void MultiMeterEditor::setChoice (const char* id, int idx)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
}

void MultiMeterEditor::resized()
{
    const int w = getWidth(), h = getHeight();
    const int tabH = roundToInt ((float) h * 0.075f);
    const int dispH = roundToInt ((float) h * 0.665f);
    const int split = roundToInt ((float) w * 0.785f);

    tabRect      = { 0, 0, w, tabH };
    dispRect     = { 0, tabH, w, dispH };
    analyzerRect = { 0, tabH, split, dispH };
    meterRect    = { split, tabH, w - split, dispH };
    panelRect    = { 0, tabH + dispH, w, h - tabH - dispH };

    const float aw = (float) analyzerRect.getWidth();
    const int ly = analyzerRect.getY() + roundToInt (0.064f * (float) dispH);
    const int lh = roundToInt (0.05f * (float) dispH);
    topRect   = { roundToInt (0.085f * aw), ly - lh / 2, roundToInt (0.11f * aw), lh };
    rangeRect = { roundToInt (0.208f * aw), ly - lh / 2, roundToInt (0.14f * aw), lh };

    auto R = [&] (float x0, float y0, float x1, float y1) { return panelR (panelRect, w, x0, y0, x1, y1); };

    const float dx[5] = { 48, 143, 238, 335, 430 };
    for (int i = 0; i < 4; ++i) detBtns[i].setBounds (R (dx[i], 754, dx[i + 1], 786).reduced (1, 0));
    const float mx[4] = { 95, 190, 285, 380 };
    for (int i = 0; i < 3; ++i) modeBtns[i].setBounds (R (mx[i], 838, mx[i + 1], 870).reduced (1, 0));

    bandsBox.setBounds   (R (526, 748, 646, 778));
    aReturnBox.setBounds (R (526, 832, 646, 862));
    aPeakBox.setBounds   (R (730, 748, 820, 778));
    aHoldBtn.setBounds   (R (729, 788, 823, 818));
    aResetBtn.setBounds  (R (729, 836, 823, 868));

    levelBox.setBounds   (R (887, 748, 1087, 778));
    lPeakBox.setBounds   (R (1088, 748, 1172, 778));
    lHoldBtn.setBounds   (R (1083, 788, 1177, 818));
    lResetBtn.setBounds  (R (1083, 836, 1177, 868));
    lReturnBox.setBounds (R (917, 834, 1057, 864));
}

//==============================================================================
// Interacción
//==============================================================================
void MultiMeterEditor::mouseDown (const MouseEvent& e)
{
    const auto pos = e.getPosition();
    dragTarget = 0;

    if (tabRect.contains (pos))
    {
        if ((float) pos.x < (float) getWidth() * 0.5f)       view = 0;
        else if ((float) pos.x < (float) getWidth() * 0.88f) view = 1;
        repaint();
        return;
    }

    // Clic sobre el medidor LUFS: reinicia el valor integrado (LU-I)
    if (meterRect.contains (pos) && (float) pos.x < (float) meterRect.getX() + (float) meterRect.getWidth() * 0.42f)
        proc.resetLoudness = true;

    if (view == 0)
    {
        if (topRect.contains (pos))        { dragTarget = 1; dragStartValue = topDb; }
        else if (rangeRect.contains (pos)) { dragTarget = 2; dragStartValue = rangeDb; }
    }
}

void MultiMeterEditor::mouseDrag (const MouseEvent& e)
{
    const float dy = (float) e.getDistanceFromDragStartY();
    if (dragTarget == 1)      topDb = (float) roundToInt (jlimit (-12.f, 30.f, dragStartValue - dy * 0.1f));
    else if (dragTarget == 2) rangeDb = (float) roundToInt (jlimit (20.f, 96.f, dragStartValue - dy * 0.3f));
}

//==============================================================================
// Análisis espectral
//==============================================================================
void MultiMeterEditor::spectrumFor (int ch, std::vector<float>& outDb)
{
    const int mask = MultiMeterProcessor::ringSize - 1;
    const int wp = proc.writePos.load (std::memory_order_acquire);
    const int start = (wp - fftSize + MultiMeterProcessor::ringSize) & mask;

    for (int i = 0; i < fftSize; ++i)
    {
        const size_t idx = (size_t) ((start + i) & mask);
        const float l = proc.ringL[idx], r = proc.ringR[idx];
        fftData[(size_t) i] = ch == 0 ? l : (ch == 1 ? r : 0.5f * (l + r));
    }
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.f);

    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    const double sr = proc.sampleRate.load();
    const double binW = sr / (double) fftSize;
    const int nb = (int) outDb.size();
    const double fHigh = bandFHigh (sr);
    const int kMax = fftSize / 2 - 1;
    const float norm = 4.f / (float) fftSize;   // ventana Hann: seno de amplitud A -> 0 dB si A = 1

    for (int b = 0; b < nb; ++b)
    {
        const double centre = bandFLow * std::pow (fHigh / bandFLow, (double) b / (double) (nb - 1));
        const double half = std::pow (fHigh / bandFLow, 0.5 / (double) (nb - 1));
        const int k0 = jlimit (1, kMax, (int) std::ceil (centre / half / binW));
        const int k1 = jlimit (1, kMax, (int) std::floor (centre * half / binW));

        float amp;
        if (k1 >= k0)
        {
            amp = 0.f;
            for (int k = k0; k <= k1; ++k) amp = jmax (amp, fftData[(size_t) k]);
        }
        else
        {
            const double pos = centre / binW;
            const int k = jlimit (1, kMax - 1, (int) pos);
            const float frac = (float) (pos - (double) k);
            amp = fftData[(size_t) k] * (1.f - frac) + fftData[(size_t) k + 1] * frac;
        }
        outDb[(size_t) b] = 20.f * std::log10 (jmax (amp * norm, 1.0e-6f));
    }
}

void MultiMeterEditor::updateSpectrum (double dt)
{
    const int nb = MultiMeterProcessor::bandCounts[jlimit (0, 2, choice ("bands"))];
    if ((int) bandDb.size() != nb)
    {
        bandDb.assign ((size_t) nb, -120.f);
        bandPeak.assign ((size_t) nb, -120.f);
        bandTimer.assign ((size_t) nb, 0.f);
        bandPow.assign ((size_t) nb, 0.f);
        tmpA.assign ((size_t) nb, -120.f);
        tmpB.assign ((size_t) nb, -120.f);
    }

    const int det = choice ("detection"), mode = choice ("mode");
    const float rate = MultiMeterProcessor::returnRates[jlimit (0, 5, choice ("aReturn"))];
    const float holdT = MultiMeterProcessor::holdTimes[jlimit (0, 4, choice ("aPeak"))];
    const bool holdOn = proc.apvts.getRawParameterValue ("aHold")->load() > 0.5f;

    if (det == 0)      spectrumFor (0, tmpA);
    else if (det == 1) spectrumFor (1, tmpA);
    else if (det == 3) spectrumFor (2, tmpA);
    else
    {
        spectrumFor (0, tmpA);
        spectrumFor (1, tmpB);
        for (size_t b = 0; b < tmpA.size(); ++b) tmpA[b] = jmax (tmpA[b], tmpB[b]);
    }

    if (analyzerReset)
    {
        std::fill (bandPeak.begin(), bandPeak.end(), -120.f);
        analyzerReset = false;
    }

    const float fdt = (float) dt;
    const double tau = mode == 0 ? 0.6 : 0.15;
    const float alpha = (float) (1.0 - std::exp (-dt / tau));

    for (size_t b = 0; b < (size_t) nb; ++b)
    {
        float target;
        if (mode == 2)
            target = tmpA[b];
        else
        {
            bandPow[b] += alpha * (std::pow (10.f, tmpA[b] * 0.1f) - bandPow[b]);
            target = 10.f * std::log10 (bandPow[b] + 1.0e-12f);
        }

        float v = bandDb[b];
        v = target > v ? target : jmax (target, v - rate * fdt);
        v = jmax (v, -120.f);
        bandDb[b] = v;

        if (v >= bandPeak[b])
        {
            bandPeak[b] = v;
            bandTimer[b] = holdT;
        }
        else if (! holdOn)
        {
            bandTimer[b] -= fdt;
            if (bandTimer[b] <= 0.f)
                bandPeak[b] = jmax (v, bandPeak[b] - rate * fdt);
        }
    }
}

void MultiMeterEditor::timerCallback()
{
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    const double dt = lastTime > 0.0 ? jlimit (0.005, 0.2, now - lastTime) : 1.0 / 30.0;
    lastTime = now;

    const int det = choice ("detection"), md = choice ("mode");
    for (int i = 0; i < 4; ++i) detBtns[i].setToggleState (i == det, dontSendNotification);
    for (int i = 0; i < 3; ++i) modeBtns[i].setToggleState (i == md, dontSendNotification);

    updateSpectrum (dt);
    repaint (dispRect);
}

//==============================================================================
// Dibujo
//==============================================================================
float MultiMeterEditor::xOfFreq (double f, Rectangle<float> plot) const
{
    const double t = std::log (f / 13.7) / std::log (25400.0 / 13.7);
    return plot.getX() + (float) t * plot.getWidth();
}

float MultiMeterEditor::yOfDb (float db, Rectangle<float> plot) const
{
    return plot.getY() + (topDb - db) / rangeDb * plot.getHeight();
}

void MultiMeterEditor::paint (Graphics& g)
{
    g.fillAll (Pal::bgMain);
    paintTabs (g);

    g.setColour (Pal::bgMain);
    g.fillRect (dispRect);
    if (view == 0) paintAnalyzer (g, analyzerRect);
    else           paintGonio (g, analyzerRect);
    paintLevels (g, meterRect);
    paintPanel (g);
}

void MultiMeterEditor::paintTabs (Graphics& g)
{
    const int w = getWidth(), h = tabRect.getHeight();
    g.setColour (Pal::bgSel);
    g.fillRect (tabRect);

    Rectangle<int> a (0, 0, roundToInt ((float) w * 0.5f), h);
    Rectangle<int> b (a.getRight(), 0, roundToInt ((float) w * 0.38f), h);
    g.setColour (view == 0 ? Pal::bgSel : Pal::tabIdle);
    g.fillRect (a);
    g.setColour (view == 1 ? Pal::bgSel : Pal::tabIdle);
    g.fillRect (b);

    g.setFont (Font (FontOptions ((float) h * 0.48f)));
    g.setColour (view == 0 ? Pal::green : Pal::dim);
    g.drawText ("ANALYZER", a, Justification::centred, false);
    g.setColour (view == 1 ? Pal::green : Pal::dim);
    g.drawText ("GONIOMETER", b, Justification::centred, false);
}

void MultiMeterEditor::paintAnalyzer (Graphics& g, Rectangle<int> area)
{
    const auto A = area.toFloat();
    const float aw = A.getWidth(), H = A.getHeight();
    const Rectangle<float> plot (A.getX() + 0.064f * aw, A.getY() + 0.128f * H, 0.892f * aw, 0.78f * H);

    // Top / Range
    g.setFont (Font (FontOptions (H * 0.034f)));
    auto drawPair = [&] (Rectangle<int> r, const String& label, const String& val)
    {
        auto lr = r.removeFromLeft (roundToInt ((float) r.getWidth() * 0.42f));
        g.setColour (Pal::axis);
        g.drawText (label, lr, Justification::centredLeft, false);
        g.setColour (Pal::green);
        g.drawText (val, r, Justification::centredLeft, false);
    };
    const String topStr = (topDb > 0.f ? "+" : "") + String ((int) topDb) + " dB";
    drawPair (topRect, "Top:", topStr);
    drawPair (rangeRect, "Range:", String ((int) rangeDb) + " dB");

    // Rejilla de dB
    g.setFont (Font (FontOptions (H * 0.027f)));
    const int step = rangeDb > 70.f ? 10 : 5;
    const int d0 = (int) std::floor (topDb / (float) step) * step;
    for (int d = d0; (float) d >= topDb - rangeDb - 0.01f; d -= step)
    {
        const float y = yOfDb ((float) d, plot);
        g.setColour (d == 0 ? Pal::axis.withAlpha (0.55f) : Pal::grid);
        g.fillRect (plot.getX(), y, plot.getWidth(), 1.f);
        g.setColour (Pal::axis);
        g.drawText (String (d), Rectangle<float> (A.getX(), y - H * 0.018f, plot.getX() - A.getX() - 8.f, H * 0.036f),
                    Justification::centredRight, false);
    }
    g.setColour (Pal::axis);
    g.drawText ("dB", Rectangle<float> (A.getX(), A.getY() + 0.064f * H - H * 0.018f, plot.getX() - A.getX() - 8.f, H * 0.036f),
                Justification::centredRight, false);

    // Rejilla de frecuencia
    const double fr[11] = { 16, 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    const char* fl[11] = { "16", "31", "62", "125", "250", "500", "1 k", "2 k", "4 k", "8 k", "16 k" };
    for (int i = 0; i < 11; ++i)
    {
        const float x = xOfFreq (fr[i], plot);
        g.setColour (Pal::grid);
        g.fillRect (x, plot.getY(), 1.f, plot.getHeight());
        g.setColour (Pal::axis);
        g.drawText (fl[i], Rectangle<float> (x - 30.f, plot.getBottom() + H * 0.012f, 60.f, H * 0.04f), Justification::centred, false);
    }
    g.drawText ("Hz", Rectangle<float> (plot.getRight() + 2.f, plot.getBottom() + H * 0.012f, 30.f, H * 0.04f),
                Justification::centredLeft, false);

    // Barras
    const int nb = (int) bandDb.size();
    if (nb > 1)
    {
        const double fHigh = bandFHigh (proc.sampleRate.load());
        const double half = std::pow (fHigh / bandFLow, 0.5 / (double) (nb - 1));
        for (int b = 0; b < nb; ++b)
        {
            const double centre = bandFLow * std::pow (fHigh / bandFLow, (double) b / (double) (nb - 1));
            const float x0 = xOfFreq (centre / half, plot), x1 = xOfFreq (centre * half, plot);
            const float bw = jmax (1.f, x1 - x0 - 1.f);

            const float top = jlimit (plot.getY(), plot.getBottom(), yOfDb (bandDb[(size_t) b], plot));
            if (top < plot.getBottom() - 0.5f)
            {
                g.setColour (Pal::bar);
                g.fillRect (x0 + 0.5f, top, bw, plot.getBottom() - top);
            }
            if (bandPeak[(size_t) b] > bandDb[(size_t) b] + 0.3f)
            {
                const float py = yOfDb (bandPeak[(size_t) b], plot);
                if (py > plot.getY() && py < plot.getBottom())
                {
                    g.setColour (Pal::green.withAlpha (0.9f));
                    g.fillRect (x0 + 0.5f, py, bw, 1.5f);
                }
            }
        }
    }
}

void MultiMeterEditor::paintGonio (Graphics& g, Rectangle<int> area)
{
    const auto A = area.toFloat();
    const auto c = A.getCentre();
    const float R = jmin (A.getWidth(), A.getHeight()) * 0.43f;
    const float d = R * 0.70711f;

    g.setColour (Pal::grid.brighter (0.4f));
    g.drawEllipse (c.x - R, c.y - R, 2.f * R, 2.f * R, 1.f);
    g.drawEllipse (c.x - R * 0.5f, c.y - R * 0.5f, R, R, 1.f);
    g.drawLine (c.x, c.y - R, c.x, c.y + R, 1.f);
    g.drawLine (c.x - R, c.y, c.x + R, c.y, 1.f);
    g.drawLine (c.x - d, c.y - d, c.x + d, c.y + d, 1.f);
    g.drawLine (c.x - d, c.y + d, c.x + d, c.y - d, 1.f);

    g.setColour (Pal::axis);
    g.setFont (Font (FontOptions (A.getHeight() * 0.04f)));
    auto lab = [&] (const char* t, float x, float y)
    {
        g.drawText (t, Rectangle<float> (x - 15.f, y - 10.f, 30.f, 20.f), Justification::centred, false);
    };
    lab ("M", c.x, c.y - R - 14.f);
    lab ("S", c.x + R + 14.f, c.y);
    lab ("L", c.x - d - 12.f, c.y - d - 12.f);
    lab ("R", c.x + d + 12.f, c.y - d - 12.f);

    const int mask = MultiMeterProcessor::ringSize - 1;
    const int wp = proc.writePos.load (std::memory_order_acquire);
    const int N = 1024;
    g.setColour (Pal::bar.brighter (0.35f).withAlpha (0.55f));
    for (int i = 0; i < N; ++i)
    {
        const size_t idx = (size_t) ((wp - N + i + MultiMeterProcessor::ringSize) & mask);
        const float l = proc.ringL[idx], r = proc.ringR[idx];
        const float s = (l - r) * 0.70711f, m = (l + r) * 0.70711f;
        const float x = c.x + jlimit (-1.f, 1.f, s) * R;
        const float y = c.y - jlimit (-1.f, 1.f, m) * R;
        g.fillRect (x - 1.f, y - 1.f, 2.f, 2.f);
    }
}

void MultiMeterEditor::paintLevels (Graphics& g, Rectangle<int> area)
{
    const auto A = area.toFloat();
    const float mx = A.getX(), mw = A.getWidth(), dy = A.getY(), H = A.getHeight();
    const float top = dy + 0.17f * H, bot = dy + 0.78f * H;
    auto yOf = [&] (float db) { return top + dbToFrac (db) * (bot - top); };
    auto X = [&] (float f) { return mx + f * mw; };

    g.setColour (Colour (0xff0b131c));
    g.fillRect (mx, dy, 1.f, H);

    const float th = H * 0.04f;
    auto text = [&] (const String& s, float x0, float x1, float cy, Colour col, Justification j)
    {
        g.setColour (col);
        g.drawText (s, Rectangle<float> (X (x0), cy - th * 0.5f, X (x1) - X (x0), th), j, false);
    };

    const float r1 = dy + 0.06f * H, r2 = dy + 0.105f * H, r3 = dy + 0.145f * H;
    g.setFont (Font (FontOptions (H * 0.033f)));
    text ("LUFS", 0.06f, 0.38f, r1, Pal::axis, Justification::centred);
    text ("L", 0.52f, 0.68f, r1, Pal::axis, Justification::centred);
    text ("R", 0.74f, 0.90f, r1, Pal::axis, Justification::centred);

    g.setFont (Font (FontOptions (H * 0.027f)));
    text ("LU-I", 0.06f, 0.22f, r2, Pal::axis, Justification::centredLeft);
    text ("LU-S", 0.06f, 0.22f, r3, Pal::axis, Justification::centredLeft);
    text ("dB", 0.36f, 0.47f, dy + 0.118f * H, Pal::axis, Justification::centredRight);
    text ("Peak", 0.64f, 0.78f, r2, Pal::axis, Justification::centred);
    text ("RMS", 0.64f, 0.78f, r3, Pal::axis, Justification::centred);

    const float hL = proc.meters[0].hold.load(), hR = proc.meters[1].hold.load();
    const float rL = proc.meters[0].rms.load(),  rR = proc.meters[1].rms.load();
    text (fmtDb (proc.lufsIntegrated.load()), 0.18f, 0.38f, r2, Pal::yellow, Justification::centredRight);
    text (fmtDb (proc.lufsShort.load()),      0.18f, 0.38f, r3, Pal::yellow, Justification::centredRight);
    text (fmtDb (hL), 0.50f, 0.64f, r2, Pal::yellow, Justification::centred);
    text (fmtDb (rL), 0.50f, 0.64f, r3, Pal::yellow, Justification::centred);
    text (fmtDb (hR), 0.78f, 0.92f, r2, Pal::yellow, Justification::centred);
    text (fmtDb (rR), 0.78f, 0.92f, r3, Pal::yellow, Justification::centred);

    // Escala
    g.setFont (Font (FontOptions (H * 0.024f)));
    for (int i = 0; i < 15; ++i)
        text (String ((int) dbTab[i]), 0.36f, 0.47f, yOf (dbTab[i]), Pal::axis, Justification::centredRight);

    // Líneas de referencia
    g.setColour (Pal::axis.withAlpha (0.5f));
    g.fillRect (X (0.52f), yOf (0.f), X (0.90f) - X (0.52f), 1.f);
    g.fillRect (X (0.52f), yOf (-12.f), X (0.90f) - X (0.52f), 1.f);
    g.fillRect (X (0.14f), yOf (-23.f), X (0.34f) - X (0.14f), 1.f);

    // Barras: azul por debajo de la referencia, amarillo por encima
    auto drawBar = [&] (float x, float w, float db, float refDb)
    {
        if (db <= -60.f) return;
        const float yv = yOf (db), yr = yOf (refDb);
        g.setColour (Pal::bar);
        if (db <= refDb)
            g.fillRect (x, yv, w, bot - yv);
        else
        {
            g.fillRect (x, yr, w, bot - yr);
            g.setColour (Pal::yellow);
            g.fillRect (x, yv, w, yr - yv);
        }
    };

    drawBar (X (0.18f), X (0.13f) - X (0.0f), proc.lufsShort.load(), -23.f);

    const int lm = choice ("level"); // 0 = Peak & RMS, 1 = Peak, 2 = RMS
    const float colW = X (0.04f) - X (0.f);
    const float cx[2][2] = { { 0.57f, 0.62f }, { 0.76f, 0.81f } };
    for (int c = 0; c < 2; ++c)
    {
        if (lm != 2)
        {
            drawBar (X (cx[c][0]), colW, proc.meters[c].peak.load(), -12.f);
            const float hv = proc.meters[c].hold.load();
            if (hv > -60.f)
            {
                g.setColour (Pal::yellow);
                g.fillRect (X (cx[c][0]), yOf (hv) - 1.f, colW, 2.f);
            }
        }
        if (lm != 1)
            drawBar (X (cx[c][1]), colW, proc.meters[c].rms.load(), -12.f);
    }

    // Correlación
    g.setFont (Font (FontOptions (H * 0.024f)));
    text ("CORRELATION", 0.05f, 0.95f, dy + 0.88f * H, Pal::axis, Justification::centred);
    const float ly = dy + 0.955f * H;
    const float x0 = X (0.10f), x1 = X (0.95f), mid = 0.5f * (x0 + x1);
    g.setColour (Colour (0xff8a2b2b));
    g.fillRect (x0, ly, mid - x0, 1.5f);
    g.setColour (Colour (0xff2f9b4a));
    g.fillRect (mid, ly, x1 - mid, 1.5f);

    const float corr = proc.correlation.load();
    const float px = mid + corr * 0.5f * (x1 - x0);
    g.setColour (corr >= 0.f ? Pal::green : Colour (0xffc04545));
    g.fillRect (jmin (mid, px), ly - 3.f, std::abs (px - mid) + 1.f, 7.f);
    g.setColour (Colours::white);
    g.fillRect (px - 1.f, ly - 7.f, 2.f, 14.f);
}

void MultiMeterEditor::paintPanel (Graphics& g)
{
    g.setColour (Pal::panel);
    g.fillRect (panelRect);

    const int w = getWidth();
    auto R = [&] (float x0, float y0, float x1, float y1) { return panelR (panelRect, w, x0, y0, x1, y1); };

    g.setColour (Pal::bgMain.withAlpha (0.5f));
    g.fillRect (R (881, 718, 882, 890));

    g.setFont (Font (FontOptions ((float) panelRect.getHeight() * 0.085f)));
    g.setColour (Pal::dim);
    auto label = [&] (const char* t, float cx, float cy)
    {
        g.drawText (t, R (cx - 100, cy - 10, cx + 100, cy + 10), Justification::centred, false);
    };
    label ("Detection", 238, 736);
    label ("Mode", 238, 820);
    label ("Analyzer Bands", 586, 736);
    label ("Return Rate", 586, 820);
    label ("Peak", 775, 736);
    label ("Level", 987, 736);
    label ("Peak", 1130, 736);
    label ("Return Rate", 987, 822);
}
