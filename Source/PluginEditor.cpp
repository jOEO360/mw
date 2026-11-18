#include "PluginEditor.h"

BassEditor::BassEditor (BassProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    addKnob   (0, "saw", "Saw");        addKnob (0, "sq", "Square");   addKnob (0, "pw", "Width");
    addKnob   (0, "pwmDepth", "PWM");   addKnob (0, "pwmRate", "PWM Rate");
    addKnob   (0, "voices", "Unison");  addKnob (0, "detune", "Detune"); addKnob (0, "width", "Stereo");
    addKnob   (0, "sub", "Sub");        addToggle (0, "subDown", "Sub -1");

    addKnob   (1, "cutoff", "Cutoff");  addKnob (1, "res", "Reso");    addKnob (1, "envAmt", "Env Amt");
    addKnob   (1, "fdec", "Env Decay"); addKnob (1, "key", "Key Trk"); addKnob (1, "drive", "Drive");
    addToggle (1, "retrig", "Retrig");

    addKnob   (2, "sizz", "Sizzle");    addKnob (2, "sizzFreq", "Sizzle Hz");
    addKnob   (2, "warm", "Warmth");    addKnob (2, "xover", "Mono Below");

    addKnob   (3, "duckLow", "Duck Lows"); addKnob (3, "duckHigh", "Duck Highs");
    addCombo  (3, "duckDiv", "Rate");      addKnob (3, "duckRel", "Release");

    addKnob   (4, "att", "Attack");     addKnob (4, "rel", "Release"); addKnob (4, "glide", "Glide");
    addKnob   (4, "master", "Master");

    setSize (840, 640);
}

void BassEditor::addKnob (int row, const juce::String& id, const juce::String& label)
{
    auto* k = knobs.add (new Knob());
    k->s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k->l.setText (label, juce::dontSendNotification);
    k->l.setJustificationType (juce::Justification::centred);
    k->l.attachToComponent (&k->s, false);
    addAndMakeVisible (k->s);
    addAndMakeVisible (k->l);
    k->a = std::make_unique<SA> (proc.apvts, id, k->s);
    rows[(size_t) row].push_back (&k->s);
}

void BassEditor::addToggle (int row, const juce::String& id, const juce::String& label)
{
    auto* t = toggles.add (new Toggle());
    t->b.setButtonText (label);
    addAndMakeVisible (t->b);
    t->a = std::make_unique<BA> (proc.apvts, id, t->b);
    rows[(size_t) row].push_back (&t->b);
}

void BassEditor::addCombo (int row, const juce::String& id, const juce::String& label)
{
    auto* c = combos.add (new Combo());
    if (auto* cp = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (id)))
        c->c.addItemList (cp->choices, 1);
    c->l.setText (label, juce::dontSendNotification);
    c->l.setJustificationType (juce::Justification::centred);
    c->l.attachToComponent (&c->c, false);
    addAndMakeVisible (c->c);
    addAndMakeVisible (c->l);
    c->a = std::make_unique<CA> (proc.apvts, id, c->c);
    rows[(size_t) row].push_back (&c->c);
}

void BassEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff15171c));
    g.setColour (juce::Colour (0xffe8e8ee));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("FATBASS", 20, 8, 300, 28, juce::Justification::left);

    g.setFont (juce::Font (12.0f, juce::Font::bold));
    for (int r = 0; r < titles.size(); ++r)
    {
        const int y = 48 + r * 118;
        g.setColour (juce::Colour (0xff6fd6ff));
        g.drawText (titles[r], 20, y, 300, 16, juce::Justification::left);
        g.setColour (juce::Colour (0xff2a2d36));
        g.drawHorizontalLine (y + 17, 20.0f, (float) getWidth() - 20.0f);
    }
}

void BassEditor::resized()
{
    for (size_t r = 0; r < rows.size(); ++r)
    {
        const int y = 48 + (int) r * 118;
        for (size_t c = 0; c < rows[r].size(); ++c)
        {
            const int x = 20 + (int) c * 80;
            auto* comp = rows[r][c];
            if (dynamic_cast<juce::Slider*> (comp))        comp->setBounds (x, y + 40, 74, 72);
            else if (dynamic_cast<juce::ComboBox*> (comp)) comp->setBounds (x, y + 52, 74, 24);
            else                                           comp->setBounds (x, y + 52, 74, 24);
        }
    }
}
