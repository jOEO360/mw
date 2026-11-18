#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

namespace
{
    constexpr float kTwoPi = 6.28318530718f;

    inline float polyBlep (float t, float dt)
    {
        if (t < dt)         { t /= dt;               return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt)  { t = (t - 1.0f) / dt;   return t * t + t + t + 1.0f; }
        return 0.0f;
    }
    inline float wrap01 (float x) { return x - std::floor (x); }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout BassProcessor::createLayout()
{
    using NR = juce::NormalisableRange<float>;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;

    auto f = [&] (const char* id, const char* name, float lo, float hi, float def, float skew = 1.0f)
    {
        v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, NR (lo, hi, 0.0f, skew), def));
    };

    // oscillators
    f ("saw",      "Saw",           0.f, 1.f, 0.9f);
    f ("sq",       "Square",        0.f, 1.f, 0.55f);
    f ("pw",       "Pulse Width",   0.05f, 0.95f, 0.5f);
    f ("pwmDepth", "PWM Depth",     0.f, 1.f, 0.25f);
    f ("pwmRate",  "PWM Rate",      0.05f, 8.f, 0.4f, 0.5f);
    f ("sub",      "Sub",           0.f, 1.f, 0.6f);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "subDown", 1 }, "Sub -1 Oct", false));
    v.push_back (std::make_unique<juce::AudioParameterInt>  (juce::ParameterID { "voices", 1 }, "Unison", 1, 7, 5));
    f ("detune",   "Detune (cents)", 0.f, 50.f, 14.f, 0.6f);
    f ("width",    "Width",         0.f, 1.f, 0.6f);

    // filter
    f ("cutoff",   "Cutoff",        40.f, 16000.f, 900.f, 0.35f);
    f ("res",      "Resonance",     0.f, 1.f, 0.2f);
    f ("envAmt",   "Env Amount",    0.f, 1.f, 0.5f);
    f ("fdec",     "Env Decay (ms)", 20.f, 1500.f, 180.f, 0.4f);
    f ("key",      "Key Track",     0.f, 1.f, 0.3f);
    f ("drive",    "Drive",         0.f, 1.f, 0.35f);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "retrig", 1 }, "Retrigger", true));

    // tone
    f ("sizz",     "Sizzle",        0.f, 1.f, 0.4f);
    f ("sizzFreq", "Sizzle Freq",   1500.f, 10000.f, 4500.f, 0.5f);
    f ("warm",     "Warmth",        0.f, 1.f, 0.4f);
    f ("xover",    "Mono Below",    50.f, 300.f, 120.f, 0.6f);

    // kick duck
    f ("duckLow",  "Duck Lows",     0.f, 1.f, 0.7f);
    f ("duckHigh", "Duck Highs",    0.f, 1.f, 0.25f);
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "duckDiv", 1 }, "Duck Rate",
                                                               juce::StringArray { "1/4", "1/8", "1/2", "1 bar" }, 0));
    f ("duckRel",  "Duck Release (ms)", 40.f, 500.f, 160.f, 0.6f);

    // env / out
    f ("att",      "Attack (ms)",   0.5f, 80.f, 1.f, 0.4f);
    f ("rel",      "Release (ms)",  5.f, 800.f, 70.f, 0.4f);
    f ("glide",    "Glide (ms)",    0.f, 300.f, 0.f, 0.5f);
    f ("master",   "Master (dB)",   -30.f, 6.f, -4.f);

    return { v.begin(), v.end() };
}

BassProcessor::BassProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool BassProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

//==============================================================================
void BassProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    osSr = sr * 2.0;

    os.initProcessing ((size_t) kChunk);
    os.reset();
    setLatencySamples ((int) std::ceil (os.getLatencyInSamples()));

    juce::dsp::ProcessSpec mono { osSr, (juce::uint32) (kChunk * 2), 1 };
    for (auto& h : sizzHp)
    {
        h.prepare (mono);
        h.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        h.reset();
    }
    juce::dsp::ProcessSpec st { sr, (juce::uint32) kChunk, 2 };
    xover.prepare (st);
    xover.reset();

    for (auto& l : ladder) l.reset();
    gate = false; env = 0.f; fenv = 0.f;
    noteStack.clear();
    duckSmLow = duckSmHigh = 1.f;
    dcX = dcY = 0.f;
    resetPhases();
}

void BassProcessor::resetPhases()
{
    for (int u = 0; u < kMaxUnison; ++u)
        phase[u] = wrap01 ((float) u * 0.6180339f);
    subPhase = 0.f;
}

void BassProcessor::readParams()
{
    auto g = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    p.saw = g ("saw");  p.sq = g ("sq");  p.pw = g ("pw");
    p.pwmDepth = g ("pwmDepth");  p.pwmRate = g ("pwmRate");
    p.subLvl = g ("sub");  p.subDown = g ("subDown") > 0.5f;
    p.voices = juce::jlimit (1, kMaxUnison, (int) std::lround (g ("voices")));
    p.detune = g ("detune");  p.width = g ("width");

    p.cutoff = g ("cutoff");  p.res = g ("res");  p.envAmt = g ("envAmt");
    p.fdec = g ("fdec");  p.key = g ("key");  p.drive = g ("drive");
    p.retrig = g ("retrig") > 0.5f;

    p.sizz = g ("sizz");  p.sizzFreq = g ("sizzFreq");
    p.warm = g ("warm");  p.xover = g ("xover");

    p.duckLow = g ("duckLow");  p.duckHigh = g ("duckHigh");
    p.duckDiv = (int) std::lround (g ("duckDiv"));
    p.duckRel = g ("duckRel");

    p.att = g ("att");  p.rel = g ("rel");  p.glide = g ("glide");
    p.master = juce::Decibels::decibelsToGain (g ("master"));

    // derived
    attInc   = 1.0f / std::max (1.0f, p.att * 0.001f * (float) sr);
    relCoef  = std::exp (-6.9f / std::max (1.0f, p.rel * 0.001f * (float) sr));
    fdecCoef = std::exp (-4.6f / std::max (1.0f, p.fdec * 0.001f * (float) sr));
    glideCoef = p.glide < 0.5f ? 1.0f : 1.0f - std::exp (-4.6f / (p.glide * 0.001f * (float) sr));

    // unison spread
    const int N = p.voices;
    uniNorm = 1.0f / std::sqrt ((float) N);
    for (int u = 0; u < N; ++u)
    {
        const float off = N == 1 ? 0.f : ((float) u / (float) (N - 1)) * 2.f - 1.f;
        detRatio[u] = std::exp2 (p.detune * off / 1200.0f);
        const float pan = off * p.width;                       // -1..1
        const float a = (pan + 1.0f) * 0.78539816f;            // 0..pi/2
        gL[u] = std::cos (a);
        gR[u] = std::sin (a);
    }

    for (auto& h : sizzHp) h.setCutoffFrequency (p.sizzFreq);
    xover.setCutoffFrequency (p.xover);
}

//==============================================================================
void BassProcessor::noteOn (int note)
{
    noteStack.erase (std::remove (noteStack.begin(), noteStack.end(), note), noteStack.end());
    const bool wasEmpty = noteStack.empty();
    noteStack.push_back (note);

    targetFreq = (float) juce::MidiMessage::getMidiNoteInHertz (note);
    gate = true;

    if (wasEmpty)
    {
        curFreq = targetFreq;
        if (env < 0.01f) resetPhases();   // consistent, punchy attack
        fenv = 1.0f;
    }
    else if (p.retrig)
        fenv = 1.0f;
}

void BassProcessor::noteOff (int note)
{
    noteStack.erase (std::remove (noteStack.begin(), noteStack.end(), note), noteStack.end());
    if (noteStack.empty())
        gate = false;
    else
        targetFreq = (float) juce::MidiMessage::getMidiNoteInHertz (noteStack.back());
}

void BassProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())                          noteOn (m.getNoteNumber());
    else if (m.isNoteOff())                    noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff()) { noteStack.clear(); gate = false; }
}

//==============================================================================
void BassProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    readParams();

    const int total = buffer.getNumSamples();

    // host tempo / position (falls back to a free-running clock if transport is stopped)
    double bpm = 120.0, ppq0 = freeRunPpq;
    bool useHost = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (pos->getIsPlaying())
                if (auto q = pos->getPpqPosition()) { ppq0 = *q; useHost = true; }
        }
    ppqPerSample = bpm / (60.0 * sr);

    auto it = midi.begin();
    int pos = 0;
    while (pos < total)
    {
        while (it != midi.end() && (*it).samplePosition <= pos)
        {
            handleMidi ((*it).getMessage());
            ++it;
        }
        int next = std::min (total, pos + kChunk);
        if (it != midi.end()) next = std::min (next, (*it).samplePosition);
        if (next <= pos) next = std::min (total, pos + 1);

        render (buffer, pos, next - pos, ppq0 + (double) pos * ppqPerSample);
        pos = next;
    }

    if (! useHost) freeRunPpq += (double) total * ppqPerSample;
}

//==============================================================================
void BassProcessor::render (juce::AudioBuffer<float>& out, int start, int n, double ppq0)
{
    float* wL = work.getWritePointer (0);
    float* wR = work.getWritePointer (1);
    work.clear (2, 0, n);
    work.clear (3, 0, n);

    const float invSr = 1.0f / (float) sr;
    const int N = p.voices;
    const float subMul = p.subDown ? 0.5f : 1.0f;

    // ---------- stage 1: oscillators + envelopes (base rate) ----------
    for (int i = 0; i < n; ++i)
    {
        curFreq += (targetFreq - curFreq) * glideCoef;

        if (gate) env = std::min (1.0f, env + attInc);
        else      { env *= relCoef; if (env < 1.0e-5f) env = 0.0f; }
        fenv *= fdecCoef;
        ampArr[(size_t) i] = env;

        const float keyMul = std::exp2 (p.key * std::log2 (std::max (curFreq, 1.0f) / 55.0f));
        const float fc = p.cutoff * std::exp2 (p.envAmt * 5.0f * fenv) * keyMul;
        cutArr[(size_t) i] = juce::jlimit (20.0f, 0.45f * (float) osSr, fc);

        lfoPhase += p.pwmRate * invSr;
        if (lfoPhase >= 1.0f) lfoPhase -= 1.0f;
        const float pw = juce::jlimit (0.05f, 0.95f, p.pw + p.pwmDepth * 0.4f * std::sin (kTwoPi * lfoPhase));

        float L = 0.f, R = 0.f;
        for (int u = 0; u < N; ++u)
        {
            const float dt = std::min (0.45f, curFreq * detRatio[u] * invSr);
            const float t  = phase[u];

            const float saw = 2.0f * t - 1.0f - polyBlep (t, dt);
            const float sq  = (t < pw ? 1.0f : -1.0f) + polyBlep (t, dt) - polyBlep (wrap01 (t + 1.0f - pw), dt);

            const float s = saw * p.saw + sq * p.sq;
            L += s * gL[u];
            R += s * gR[u];

            phase[u] += dt;
            if (phase[u] >= 1.0f) phase[u] -= 1.0f;
        }
        wL[i] = L * uniNorm * 0.5f;
        wR[i] = R * uniNorm * 0.5f;

        subPhase += curFreq * subMul * invSr;
        if (subPhase >= 1.0f) subPhase -= 1.0f;
        subArr[(size_t) i] = std::sin (kTwoPi * subPhase) * p.subLvl;
    }

    // ---------- stage 2: drive + ladder filter + sizzle band (2x oversampled) ----------
    {
        auto block = juce::dsp::AudioBlock<float> (work).getSubBlock (0, (size_t) n);
        auto up = os.processSamplesUp (block);
        const int un = (int) up.getNumSamples();

        float* d[4] = { up.getChannelPointer (0), up.getChannelPointer (1),
                        up.getChannelPointer (2), up.getChannelPointer (3) };

        const float preGain  = 1.0f + p.drive * 6.0f;
        const float postGain = 1.0f / (1.0f + p.drive * 2.0f);
        const float k = p.res * 3.8f;

        for (int j = 0; j < un; ++j)
        {
            const float fc = cutArr[(size_t) (j >> 1)];
            const float g  = std::tan (juce::MathConstants<float>::pi * fc / (float) osSr);
            const float G  = g / (1.0f + g);

            for (int ch = 0; ch < 2; ++ch)
            {
                const float x = d[ch][j] * preGain;

                // sizzle: high band of the *pre-filter* signal, hard saturated.
                // independent of the LPF so the main tone can stay dark/warm.
                const float h = sizzHp[ch].processSample (0, x);
                d[ch + 2][j] = std::tanh (h * 4.0f);

                d[ch][j] = ladder[ch].process (x, G, k) * postGain;
            }
        }
        os.processSamplesDown (block);
    }

    // ---------- stage 3: band split, warmth, kick ducking, output (base rate) ----------
    const float* mL = work.getReadPointer (0);
    const float* mR = work.getReadPointer (1);
    const float* zL = work.getReadPointer (2);
    const float* zR = work.getReadPointer (3);
    float* oL = out.getWritePointer (0) + start;
    float* oR = out.getWritePointer (1) + start;

    static const double divBeats[4] = { 1.0, 0.5, 2.0, 4.0 };
    const double div = divBeats[juce::jlimit (0, 3, p.duckDiv)];
    const double relSamples = std::max (1.0, (double) p.duckRel * 0.001 * sr);
    const float smCoef = 1.0f - std::exp (-1.0f / (0.0015f * (float) sr));
    const float dcR = 1.0f - kTwoPi * 5.0f / (float) sr;
    const float warmG = 1.0f + p.warm * 3.0f;
    const float warmB = 0.25f * p.warm;
    const float warmBias = std::tanh (warmB);
    const float warmNorm = 1.0f / std::sqrt (warmG);

    for (int i = 0; i < n; ++i)
    {
        const float a = ampArr[(size_t) i];

        float lowL, highL, lowR, highR;
        xover.processSample (0, mL[i] * a, lowL, highL);
        xover.processSample (1, mR[i] * a, lowR, highR);

        float low = 0.5f * (lowL + lowR) + subArr[(size_t) i] * a;     // mono, clean low end
        low = (std::tanh (low * warmG + warmB) - warmBias) * warmNorm; // asymmetric = even harmonics = warmth

        const float dcOut = low - dcX + dcR * dcY;                     // remove DC from asymmetric shaper
        dcX = low; dcY = dcOut; low = dcOut;

        const float sizzAmt = p.sizz * 0.5f * a;
        highL += zL[i] * sizzAmt;
        highR += zR[i] * sizzAmt;

        // kick duck: tempo-synced, snaps down on the beat, recovers over duckRel
        double ph = std::fmod (ppq0 + (double) i * ppqPerSample, div);
        if (ph < 0.0) ph += div;
        const double x = (ph / ppqPerSample) / relSamples;             // 0..1 over release
        const float d = x >= 1.0 ? 0.0f : (float) ((1.0 - x) * (1.0 - x));
        duckSmLow  += ((1.0f - p.duckLow  * d) - duckSmLow)  * smCoef;
        duckSmHigh += ((1.0f - p.duckHigh * d) - duckSmHigh) * smCoef;

        const float yL = (low * duckSmLow + highL * duckSmHigh) * p.master;
        const float yR = (low * duckSmLow + highR * duckSmHigh) * p.master;
        oL[i] = std::tanh (yL);
        oR[i] = std::tanh (yR);
    }
}

//==============================================================================
juce::AudioProcessorEditor* BassProcessor::createEditor() { return new BassEditor (*this); }

void BassProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void BassProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BassProcessor(); }
