#pragma once

#include <juce_graphics/juce_graphics.h>

/** Palette IDGems : noir texturé, orange, touche de bleu, une couleur par module. */
namespace Palette
{
    inline const juce::Colour background  { 0xff0b0b0d };
    inline const juce::Colour panel       { 0xff141418 };
    inline const juce::Colour panelEdge   { 0xff2a2a31 };
    inline const juce::Colour text        { 0xffe8e6e3 };
    inline const juce::Colour textDim     { 0xff8a8790 };
    inline const juce::Colour orange      { 0xffff6a1a };
    inline const juce::Colour blue        { 0xff2f8cff };
    inline const juce::Colour red         { 0xffff3b30 };

    // Une couleur par module
    inline const juce::Colour stomach     { 0xffff6a1a }; // Estomac : orange
    inline const juce::Colour map         { 0xffe8e6e3 }; // Carte : blanc
    inline const juce::Colour sequencer   { 0xff2f8cff }; // Séquenceur : bleu
    inline const juce::Colour ouroboros   { 0xffff3b30 }; // Ouroboros : rouge-orange
    inline const juce::Colour fx          { 0xffa45bff }; // FX : violet
    inline const juce::Colour modulation  { 0xff19e3d6 }; // Modulation : cyan
    inline const juce::Colour output      { 0xff9aa5b1 }; // Sortie : gris acier
}
