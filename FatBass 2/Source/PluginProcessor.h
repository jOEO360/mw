#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

class BassProcessor : public juce::AudioProcessor
{
public:
    BassProcessor();
    ~BassProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FatBass"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    static constexpr int kChunk = 512;
    static constexpr int kMaxUnison = 7;

    //==========================================================================
    struct Params
    {
        float saw, sq, pw, pwmDepth, pwmRate, subLvl;
        bool  subDown;
        int   voices;
        float detune, width;
        float cutoff, res, envAmt, fdec, key, drive;
        bool  retrig;
        float sizz, sizzFreq, warm, xover;
        float duckLow, duckHigh, duckRel;
        int   duckDiv;
        float att, rel, glide, master;
    } p {};

    struct Ladder
    {
        float s[4] {};
        float process (float x, float G, float k)
        {
            const float G2 = G * G, G4 = G2 * G2, a = 1.0f - G;
            const float S = a * (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]);
            const float y4 = (G4 * x + S) / (1.0f + k * G4);
            float in = std::tanh (x - k * y4);
            for (int i = 0; i < 4; ++i)
            {
                const float v = (in - s[i]) * G;
                const float y = v + s[i];
                s[i] = y + v;
                in = y;
            }
            return in;
        }
        void reset() { for (auto& v : s) v = 0.0f; }
    };

    void readParams();
    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note);
    void noteOff (int note);
    void resetPhases();
    void render (juce::AudioBuffer<float>& out, int start, int n, double ppq0);

    // engine
    double sr = 44100.0, osSr = 88200.0;
    juce::dsp::Oversampling<float> os { 4, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false };
    juce::AudioBuffer<float> work { 4, kChunk };
    std::array<float, kChunk> ampArr {}, cutArr {}, subArr {};

    Ladder ladder[2];
    juce::dsp::StateVariableTPTFilter<float> sizzHp[2];
    juce::dsp::LinkwitzRileyFilter<float> xover;

    // voice (monophonic, legato-capable)
    std::vector<int> noteStack;
    bool  gate = false;
    float env = 0.f, fenv = 0.f, curFreq = 55.f, targetFreq = 55.f;
    float phase[kMaxUnison] {};
    float subPhase = 0.f, lfoPhase = 0.f;
    float detRatio[kMaxUnison] {}, gL[kMaxUnison] {}, gR[kMaxUnison] {}, uniNorm = 1.f;

    // ducking / host sync
    double freeRunPpq = 0.0, ppqPerSample = 0.0;
    float duckSmLow = 1.f, duckSmHigh = 1.f;

    // dc blocker
    float dcX = 0.f, dcY = 0.f;

    // derived coefficients
    float attInc = 0.1f, relCoef = 0.99f, fdecCoef = 0.999f, glideCoef = 1.f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BassProcessor)
};
