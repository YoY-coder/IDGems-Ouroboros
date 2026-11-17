#include "Params.h"

namespace P
{
    static const Info table[] =
    {
       #define OURO_INFO(id, name, mod, kind, mn, mx, def, skew, choices, rnd) \
            { #id, name, Module::mod, Kind::kind, (float) (mn), (float) (mx), (float) (def), (float) (skew), choices, rnd },
        OUROBOROS_PARAMS (OURO_INFO)
       #undef OURO_INFO
    };

    static_assert (sizeof (table) / sizeof (table[0]) == (size_t) count);

    const Info& info (int index)
    {
        jassert (index >= 0 && index < count);
        return table[index];
    }

    int indexOf (const juce::String& id)
    {
        for (int i = 0; i < count; ++i)
            if (id == table[i].id)
                return i;
        return -1;
    }

    const char* moduleName (Module m)
    {
        switch (m)
        {
            case Module::Stomach: return "Estomac";
            case Module::Seq:     return "Sequenceur";
            case Module::Ouro:    return "Ouroboros";
            case Module::Fx:      return "FX";
            case Module::Mod:     return "Modulation";
            case Module::Out:     return "Sortie";
            case Module::Rand:    return "Random";
        }
        return "";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (int i = 0; i < count; ++i)
        {
            const auto& p = table[i];
            const juce::ParameterID pid { p.id, 1 };

            switch (p.kind)
            {
                case Kind::F:
                    layout.add (std::make_unique<juce::AudioParameterFloat> (pid, p.name, p.range(), p.def));
                    break;
                case Kind::I:
                    layout.add (std::make_unique<juce::AudioParameterInt> (pid, p.name, (int) p.min, (int) p.max, (int) p.def));
                    break;
                case Kind::B:
                    layout.add (std::make_unique<juce::AudioParameterBool> (pid, p.name, p.def > 0.5f));
                    break;
                case Kind::C:
                    layout.add (std::make_unique<juce::AudioParameterChoice> (
                        pid, p.name, juce::StringArray::fromTokens (p.choices, "|", ""), (int) p.def));
                    break;
            }
        }
        return layout;
    }

    Values defaults()
    {
        Values v {};
        for (int i = 0; i < count; ++i)
            v[(size_t) i] = table[i].def;
        return v;
    }
}
