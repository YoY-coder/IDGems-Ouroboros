#pragma once

#include "core/Library.h"
#include "core/Params.h"
#include "analysis/Analysis.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>

class OuroborosProcessor;

/** Un fragment + le buffer qu'il référence (modèle côté UI / threads de fond). */
struct FragRec
{
    SampleDataPtr buf;
    Fragment f;
};

struct SourceSlot
{
    bool used = false;
    bool active = true;
    bool busy = false;
    juce::String name;
    juce::File file;           // vide si capture
    float gain = 1.0f;
    float weight = 1.0f;
    juce::Colour colour;
    SliceSettings slice;
    SampleDataPtr audio;
    std::array<SampleDataPtr, 4> bands;
    std::vector<FragRec> frags;
};

struct GenNode
{
    int id = 0;
    int parent = -1;
    int number = 0;
    bool faded = false;
    uint32_t seed = 0;
    std::vector<FragRec> frags;      // fragments propres à cette génération (survivants + nouveaux + croisements)
    SampleDataPtr audio;             // rendu complet de la génération (pour l'export)
    juce::ValueTree params;          // réglages au moment de la génération
    Pattern pattern;
};

struct HistoryEntry
{
    uint32_t seed = 0;
    P::Values values {};
    Pattern pattern;
    bool favourite = false;
    juce::String label;
};

struct PresetInfo
{
    juce::String name;
    juce::StringArray tags;
    bool favourite = false;
    bool factory = false;
    juce::File file;
    int factoryIndex = -1;
};

/**
    Chef d'orchestre côté interface (thread UI) + travaux de fond.
    Toutes ses données sont lues/écrites sur le thread UI ; les threads de fond reçoivent des copies
    et renvoient leurs résultats via MessageManager::callAsync.
*/
class Controller : public juce::ChangeBroadcaster, private juce::Timer
{
public:
    explicit Controller (OuroborosProcessor&);
    ~Controller() override;

    // --- Estomac --------------------------------------------------------------------------
    void swallow (int slot = -1);
    void loadFiles (const juce::StringArray& paths, int firstSlot = -1);
    void clearSlot (int slot);
    void setSlotActive (int slot, bool);
    void setSlotGain (int slot, float);
    void setSlotWeight (int slot, float);
    void setSlotSlice (int slot, const SliceSettings&);
    void reslice (int slot);
    int firstFreeSlot() const;
    std::array<SourceSlot, kNumSlots> slots;
    int selectedSlot = 0;

    // --- Carte ------------------------------------------------------------------------------
    static constexpr int kMaskSize = 64;
    std::array<float, kMaskSize * kMaskSize> mask {};
    bool maskEmpty = true;
    int axisX = 0, axisY = 3;
    void paintMask (float x, float y, float radius, float amount); // amount < 0 = gomme
    void clearMask();
    void setAxes (int x, int y);
    void toggleLike (uint32_t fragId);
    void audition (int fragIndex);
    std::shared_ptr<const LibrarySnapshot> getLibrary() const { return library; }
    uint32_t selA = 0, selB = 0;      // sélection pour le croisement (ids)
    std::set<uint32_t> likedIds;

    // --- Séquenceur / modulation (copies UI, publiées vers l'audio) --------------------------
    Pattern pattern;
    ModSnapshot mods;
    void publishPattern();
    void publishMods();
    void randomizePattern (float amount, juce::Random& rng);
    void addRoute (int source, int param, float depth);
    void removeRoutes (int param);
    std::vector<ModRoute> routesFor (int param) const;
    void moveFx (int from, int to);

    // --- Croisement ---------------------------------------------------------------------------
    void crossSelected();

    // --- Générations --------------------------------------------------------------------------
    void newGeneration();
    void selectNode (int id);
    std::vector<GenNode> tree;
    int currentNode = 0;
    int generationCounter = 0;   // nombre de mutations (fissures de l'anneau)
    bool generating = false;

    // --- Random -----------------------------------------------------------------------------
    std::array<bool, P::count> locks {};
    std::array<bool, 7> moduleLocks {};
    std::array<std::pair<float, float>, P::count> ranges {};
    std::vector<HistoryEntry> history;
    uint32_t lastSeed = 0;
    uint32_t nextSeed();
    void randomizeAll (uint32_t seed);
    void randomizeModule (P::Module m, uint32_t seed);
    void recallHistory (int index);
    void toggleFavourite (int index);
    void setRange (int param, float lo, float hi);

    // --- Undo / A-B ---------------------------------------------------------------------------
    void undo();
    void redo();
    void beginGesture();
    void toggleAB();
    void copyAtoB();
    bool abIsB = false;

    // --- MIDI learn ---------------------------------------------------------------------------
    void startMidiLearn (int param);
    void clearMidiLearn (int param);
    int midiLearnCcFor (int param) const;

    // --- Export ------------------------------------------------------------------------------
    juce::File getExportFolder() const;
    void setExportFolder (const juce::File&);
    juce::File getIdgemsFolder() const;
    void setIdgemsFolder (const juce::File&);
    void exportGeneration();
    void exportLiked();
    void exportLoop();
    void exportBatch (int count);
    juce::File writeFragmentForDrag (uint32_t fragId);
    juce::File writeLoopForDrag();

    // --- Presets -------------------------------------------------------------------------------
    std::vector<PresetInfo> listPresets();
    void loadPreset (const PresetInfo&);
    void savePreset (const juce::String& name, const juce::StringArray& tags, bool favourite);
    void setPresetFavourite (const PresetInfo&, bool);
    void applyPatternAndMods (const juce::ValueTree& extra);
    juce::String currentPresetName { "Init" };

    // --- État -----------------------------------------------------------------------------------
    juce::ValueTree saveExtraState() const;
    void restoreExtraState (const juce::ValueTree&);
    /** Thread-safe : état frais sur le thread UI, sinon la dernière copie en cache. */
    juce::ValueTree getExtraStateForSaving();
    void restoreExtraStateAsync (const juce::ValueTree&);

    // --- Statut -------------------------------------------------------------------------------
    juce::String status;
    juce::uint32 statusTime = 0;
    void setStatus (const juce::String&);

    int64_t memoryUsed() const;
    int64_t memoryLimit() const;
    float captureFilledSeconds() const;
    bool isBusy() const { return busyJobs > 0; }

    void publishLibrary();
    P::Values currentValues() const;
    void applyValues (const P::Values&, bool respectLocks);

    OuroborosProcessor& proc;
    juce::AudioFormatManager formats;
    juce::UndoManager& undoManager;

private:
    void timerCallback() override;
    void runJob (std::function<void()> job);
    void onMessageThread (std::function<void()> f);
    void finishSlice (int slot, std::vector<FragRec> frags, std::array<SampleDataPtr, 4> bands);
    void fadeOldGenerations (int64_t bytesNeeded);
    void ensureRing();
    uint32_t newFragId() { return ++fragIdCounter; }
    std::vector<FragRec> analyseRegions (const SampleDataPtr& buf, const std::vector<SliceRegion>&,
                                         int slot, int gen, juce::Colour colour, float gain);
    static juce::Colour slotColour (int slot);
    juce::String bpmTag() const;
    void writeWav (const juce::File&, const juce::AudioBuffer<float>&, double sr);
    void writeJson (const juce::File&, const juce::var&);
    juce::AudioBuffer<float> renderOffline (const P::Values& values, const Pattern& pat, int bars, uint32_t seed,
                                           std::shared_ptr<const LibrarySnapshot> lib, const ModSnapshot& modsCopy);
    void updateCaptureRingIfNeeded();
    void applyFactoryPreset (int index);

    std::shared_ptr<const LibrarySnapshot> library;
    uint32_t libraryVersion = 0;
    std::atomic<uint32_t> fragIdCounter { 0 };
    std::unique_ptr<juce::ThreadPool> pool;
    std::shared_ptr<std::atomic<bool>> alive;
    std::atomic<int> busyJobs { 0 };
    bool libraryDirty = false;
    juce::ValueTree abOther;
    int lastCaptureSeconds = -1;
    int64_t lastAutoBar = -1;
    int lastBudgetChoice = -1;
    juce::Random random;
    std::unique_ptr<juce::PropertiesFile> settings;
    juce::CriticalSection cacheLock;
    juce::ValueTree cachedExtra;
    bool stateDirty = true;
};
