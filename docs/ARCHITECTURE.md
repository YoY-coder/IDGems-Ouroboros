# IDGems Ouroboros — Proposition d'architecture

> Statut : **proposition à valider** avant la phase 1. Rien n'est codé.

---

## 0. Règle d'or : mémoire fixe, aucune accumulation

> Demande explicite : le plugin ne doit **jamais** remplir la mémoire ni le disque,
> et ne doit **jamais** produire d'audio non voulu (ni fichiers, ni son).

1. **Un seul réservoir audio de taille fixe** (`AudioPool`), alloué une fois au chargement.
   Réglage « Mémoire max » : **48 / 96 / 192 Mo** (défaut 96 Mo par instance).
   Tout l'audio du plugin vit dedans : capture, sources, générations, croisements.
   Le réservoir **ne grandit jamais** : quand il est plein, on recycle.
2. **Stockage 16 bits** à l'intérieur du réservoir (le traitement reste en float) :
   96 Mo ≈ 8 min de stéréo à 48 kHz au total, toutes sources confondues.
3. **Capture = buffer circulaire** : il s'écrase lui-même en continu (5–120 s), il ne s'allonge jamais.
4. **Générations = file tournante** : seules les N dernières gardent leur audio (N limité par le réservoir).
   Quand il faut de la place, la plus ancienne génération **non likée** est effacée.
   Dans l'arbre, elle reste visible comme « branche fanée » (recette = seed + réglages, quelques Ko), sans audio.
5. **Fragments = références** (début/longueur dans une source), jamais des copies.
   Les croisements A×B créent de l'audio : ils prennent une place dans le réservoir et sont recyclés pareil.
6. **Fichiers glissés** : lus et copiés dans le réservoir (60 s max par source, tronqués au-delà) ; le fichier d'origine n'est jamais modifié.
7. **Disque : rien n'est écrit sans action de ta part.** Seuls « Exporter » et « Batch » écrivent des WAV, dans le dossier que tu choisis.
   Le glisser-déposer vers Ableton utilise un fichier temporaire unique, effacé ensuite ; le dossier temporaire est vidé à chaque ouverture du plugin.
8. **Projet Ableton** : on sauvegarde les réglages, les chemins des fichiers glissés et **uniquement les fragments likés** (compressés, plafond 16 Mo).
   La capture et les générations ne sont pas sauvegardées : le projet reste léger.
9. **Jauge mémoire** visible dans l'Estomac (ex. « 41 / 96 Mo »).
10. **Pas de son non voulu** : la boucle Ouroboros est **interne** (rendu hors ligne vers le réservoir), jamais réinjectée dans la sortie en direct, donc aucun larsen possible.
    Le moteur ne joue rien tant que le séquenceur est arrêté ou qu'aucune note n'est reçue ; chaque génération est normalisée ; limiteur obligatoire en fin de chaîne ; bouton Panic.

---

## 1. Vue d'ensemble

```
             ┌──────────────────────────── THREAD AUDIO (temps réel) ────────────────────────────┐
 Entrée ───► │ Capture ring ─┐                                                                   │
 Sidechain ─►│               │      ┌──────────────┐   ┌──────────┐   ┌──────────────┐          │
 MIDI ──────►│ MidiRouter ───┼────► │ Recombinator │──►│ FX Rack  │──►│ Output chain │──► Sortie │
             │               │      │ (séquenceur  │   │ (7 slots)│   │ OTT→clip→lim │           │
             │               │      │  + voix)     │   └──────────┘   └──────┬───────┘           │
             │               │      └──────▲───────┘                         │ tap               │
             └───────────────┼─────────────┼─────────────────────────────────┼───────────────────┘
                             │ SPSC FIFO   │ snapshot atomique               │ SPSC FIFO
             ┌───────────────▼─────────────┴─────────────────────────────────▼───────────────────┐
             │                       THREADS DE FOND (ThreadPool, 2–3 workers)                    │
             │  Ingest (décodage fichiers, gel capture) → Slicer → Analyzer → FragmentLibrary     │
             │  GenerationEngine (rendu offline des générations, mutation, sélection, arbre)      │
             │  CrossRenderer (vocoder / granulaire / morph / collage → nouveaux fragments)       │
             │  Exporter (WAV 24 bits + JSON, batch)                                              │
             └────────────────────────────────────────────────────────────────────────────────────┘
             ┌──────────────────────────── THREAD UI (message thread, 60 fps) ────────────────────┐
             │  Éditeur redimensionnable, carte 2D, anneau Ouroboros, arbre, rack, presets        │
             └────────────────────────────────────────────────────────────────────────────────────┘
```

**Principe clé :** le thread audio ne fait *que* lire des données déjà prêtes.
Tout ce qui alloue, analyse, décode ou écrit sur disque se fait en fond, puis
est « publié » au thread audio par un échange de pointeur atomique.

---

## 2. Modèle de threads et échanges

| Thread | Rôle | Règles |
|---|---|---|
| **Audio** (`processBlock`) | capture, séquenceur, voix, FX, sortie, mètres | aucune allocation, aucun mutex, aucune I/O |
| **UI** (message thread JUCE) | dessin, interactions, envoi de commandes | ne touche jamais aux buffers audio directement |
| **Workers** (`juce::ThreadPool`, 2–3 threads, priorité basse) | décodage, découpe, analyse, rendu des générations, croisements, export | peuvent allouer librement |

### Mécanismes d'échange
1. **`FragmentLibrary` immuable + swap atomique (RCU simplifié)**
   La bibliothèque de fragments est un objet *en lecture seule*. Un worker
   construit une nouvelle version, puis la publie via un pointeur atomique.
   Le thread audio prend une référence au début de chaque bloc.
   L'ancienne version n'est **jamais détruite dans le thread audio** : elle
   part dans une « file de poubelle » vidée par le thread UI (timer).
2. **Files SPSC lock-free** (`juce::AbstractFifo`) pour :
   - commandes UI → audio (jouer un fragment, panic, déclencher génération…) ;
   - événements audio → UI (pas courant, fragment joué, niveaux des mètres, spectre) ;
   - audio → worker : copie de la sortie du moteur pour le ré-échantillonnage Ouroboros.
3. **Paramètres** : `AudioProcessorValueTreeState` (atomiques, automatisables
   dans Ableton). Les données non automatisables (sources, zone peinte, arbre)
   vivent dans un `ValueTree` côté UI/workers et sont publiées en snapshot.
4. **Pré-allocation** : pool fixe de voix (32), buffers de capture alloués
   au `prepareToPlay`, taille max fixée par le réglage 5–120 s.

---

## 3. Classes principales

### Noyau
- `OuroborosProcessor` (`AudioProcessor`) — bus : main stereo in/out + sidechain stereo (optionnel), `acceptsMidi = true`.
- `OuroborosEditor` (`AudioProcessorEditor`) — redimensionnable, ratio fixe.
- `ParameterLayout` — déclaration de tous les paramètres APVTS (IDs stables pour les presets).
- `StateSerializer` — sauvegarde/rappel (réglages + sources : intégrées ou référencées).

### Estomac (module 1)
- `AudioPool` — réservoir audio unique de taille fixe (16 bits), allocation par blocs, recyclage du plus ancien non liké.
- `CaptureRing` — buffer circulaire stéréo (dans le réservoir), gel instantané (« Avaler ») = copie lancée en fond.
- `AudioFileIngestor` — décodage WAV/AIFF/FLAC/MP3 (`AudioFormatManager`), resampling vers le SR de l'hôte.
- `Source` — audio + gain, couleur, poids, actif, mode de découpe, génération d'origine.
- `SourceBank` — 8 emplacements max.

### Découpe (module 2)
- `Slicer` (interface) → `TransientSlicer`, `GridSlicer`, `BandSlicer` (bancs de filtres Linkwitz-Riley), `RandomSlicer`.
- `ZeroCrossFader` — recale les bords sur passage par zéro + fondus 2–5 ms.

### Analyse et carte (module 3)
- `FragmentAnalyzer` — centroïde, flatness, pitch (YIN), RMS/crête, longueur.
- `Fragment` — référence vers l'audio source (offset/longueur, **pas de copie**) + descripteurs + flags (liké, âge, génération).
- `FragmentLibrary` — max 512 fragments, index spatial (grille 2D) pour « plus proche voisin ».
- `PaintMask` — grille 64×64 de densités (pinceau/gomme), convertie en poids.

### Recombinaison (module 4)
- `StepSequencer` — 1–64 pas, 1/4 à 1/32, swing, synchro `AudioPlayHead`.
- `PickRules` — mélange aléatoire / ressemblance / contraste.
- `StepParams` — proba, ratchets, reverse, pitch, longueur (+ plages random).
- `VoicePool` / `FragmentVoice` — lecture avec pitch (interpolation Hermite), reverse, enveloppe.
- `MidiKeyMapper` — mode « clavier = carte ».
- `CrossRenderer` — croisements A×B **rendus en fond** et ajoutés comme nouveaux fragments.

### Ouroboros (module 5)
- `GenerationEngine` — rééchantillonne la sortie, crée la source « génération N+1 », applique mutation, ADN, mort/survie.
- `GenerationTree` — arbre (nœud = snapshot des sources + paramètres), retour à une branche.

### Random & modulation (module 6)
- `Randomizer` — seed (`juce::Random` déterministe), dosage, plages, cadenas, historique + favoris.
- `MacroSystem` — Chaos + 8 macros.
- `LFO` (formes dessinées au crayon, tables précalculées), `RandomMod` (S&H).
- `ModMatrix` — sources → cibles, appliquée par bloc (pas par échantillon, sauf filtres).

### FX & sortie (modules 7–8)
- `FxSlot` (interface) → `Bitcrush`, `Distortion`, `MultiFilter`, `PitchFreqShift`, `Stutter`, `Delay`, `Reverb`.
- `FxRack` — ordre modifiable : l'ordre est un tableau d'indices échangé atomiquement (aucune réallocation).
- `OutputChain` — `OTT` (3 bandes) → `SoftClip` → `Limiter` (lookahead), HP, mono basses, mètres, Panic.

### Export & confort (modules 9–10)
- `Exporter` — WAV 24 bits (`BPM` dans le nom), batch par seeds, **sidecar JSON** (`FragmentMeta`).
- `DragSource` — glisser vers Ableton via `performExternalDragDropOfFiles` (fichier temporaire écrit avant le drag).
- `PresetManager` (tags, favoris), `UndoManager` JUCE, `ABCompare`, `MidiLearn`, `Tooltips`, `Shortcuts`.

### UI
- `LookAndFeelIDGems` — palette noir/orange/bleu, aberration chromatique.
- `ScaledRoot` — tout dessiné dans un espace logique 1600×1000 puis mis à l'échelle (`AffineTransform`).
- Composants : `StomachPanel`, `FragmentMapView`, `SequencerView`, `OuroborosRing`, `GenerationTreeView`, `FxRackView`, `OutputPanel`, `RandomBar`, `ModPanel`.

---

## 4. Structure des fichiers

```
IDGems-Ouroboros/
├─ CMakeLists.txt              # JUCE via CMake (FetchContent) → génère la solution Visual Studio
├─ README.md                   # instructions de build pas à pas
├─ docs/ARCHITECTURE.md
├─ assets/                     # polices, textures noir, icônes (SVG)
├─ source/
│  ├─ PluginProcessor.h/.cpp
│  ├─ PluginEditor.h/.cpp
│  ├─ core/        Parameters, StateSerializer, LockFreeQueue, AtomicSnapshot, GarbageQueue
│  ├─ stomach/     CaptureRing, AudioFileIngestor, Source, SourceBank
│  ├─ slicing/     Slicer*, ZeroCrossFader
│  ├─ analysis/    FragmentAnalyzer, Fragment, FragmentLibrary, PaintMask
│  ├─ engine/      StepSequencer, PickRules, VoicePool, MidiKeyMapper, CrossRenderer
│  ├─ ouroboros/   GenerationEngine, GenerationTree
│  ├─ modulation/  Randomizer, MacroSystem, LFO, RandomMod, ModMatrix
│  ├─ fx/          FxRack + 7 effets
│  ├─ output/      OutputChain, OTT, SoftClip, Limiter, Meters
│  ├─ export/      Exporter, FragmentMeta (JSON), DragSource
│  ├─ presets/     PresetManager, ABCompare, MidiLearn
│  └─ ui/          LookAndFeel, ScaledRoot, panels/, widgets/
└─ tests/          tests unitaires (Catch2) des algos : slicers, analyse, sélection
```

On utilise **CMake** plutôt que Projucer : un seul fichier texte, versionnable,
et Visual Studio l'ouvre directement (« Ouvrir un dossier »).

---

## 5. Maquette de l'interface (taille de référence 1600×1000)

```
┌──────────────────────────────────────────────────────────────────────────────────────────────┐
│ ⟲ IDGems OUROBOROS   [Preset ▾ Glitch Soup ★]  A|B  ↶ ↷   [🎲 RANDOM ▮▮▮▯ 70%] seed 48213 🔒 │ ← barre haute
├──────────────┬─────────────────────────────────────────────────┬─────────────────────────────┤
│ ESTOMAC  ▣   │  CARTE DES FRAGMENTS            X:[Brillance▾]  │   OUROBOROS                  │
│ (orange)     │  ┌───────────────────────────────────────────┐  │      ╭──────────╮            │
│ ● Capture 30s│  │  ·   ·  ●       ·        ·   ·    ·       │  │    ╭─╯ ~serpent~ ╰─╮         │
│ [ AVALER ]   │  │    ●  · ░░░░░·  ·  ●                ·     │  │    │   GEN  07      │         │
│ ─────────────│  │  ·    ░░▓▓▓▓░░    ·      ·   ·            │  │    ╰─╮  ⚡fissures ╭─╯         │
│ 1 ▮ in-cap  ◐│  │     · ░▓▓██▓▓░  ·    ●        ·    ●      │  │      ╰────────╯             │
│ 2 ▮ synth.wav│  │  ●    ░░▓▓▓░░     ·       ·               │  │ Mode [AUTO|MANUEL] /4 mes.  │
│ 3 ▯ gen-05   │  │     ·    ░░    ·     ·         ·   ·      │  │ Mutation ◯  ADN ◯  Mort ◯   │
│ … (8 max)    │  └───────────────────────────────────────────┘  │ Max gén. [16]               │
│ Découpe:     │  Y:[Bruit▾]  🖌 pinceau  ⌫ gomme  ♥ liker        │ [ NOUVELLE GÉNÉRATION ]     │
│ [Trans▾] ◯◯  │                                                 │ ARBRE  0─1─2─┬─3─5─7●       │
│ 📂 IDGems rec│                                                 │              └─4─6          │
├──────────────┴─────────────────────────────────────────────────┴─────────────────────────────┤
│ SÉQUENCEUR (bleu)  Pas [1/16▾] Long [16]  Règles: Aléa ◯ Ressemblance ◯ Contraste ◯  Swing ◯ │
│ ▮▯▮▮▯▮▯▮▮▮▯▮▯▮▮▯   ligne: proba / ratchet / reverse / pitch / longueur  [Croisement: Morph▾]│
├───────────────────────────────────────────────┬──────────────────────────────────────────────┤
│ FX RACK (violet) ⇅ glisser pour réordonner     │ MODULATION  LFO1 LFO2 LFO3 LFO4  RND1 RND2   │
│ [Crush][Dist][Filt][Shift][Stut][Dly][Verb]    │ ∿ éditeur crayon   CHAOS ◯◯  M1..M8 ◯◯◯◯◯◯◯◯ │
├───────────────────────────────────────────────┴──────────────────────────────────────────────┤
│ SORTIE  OTT ◯  Clip ◯  Lim ◯  HP ◯  Mono<Hz ◯  Dry/Wet ◯  Gain ◯  ▮▮▮▮▯ ▮▮▮▯▯  [PANIC] [EXPORT]│
└──────────────────────────────────────────────────────────────────────────────────────────────┘
                                                                                  coin ◢ resize
```

- Tailles prédéfinies : 75 %, 100 %, 125 %, 150 %, 200 % + coin de redimensionnement (ratio 1,6 conservé).
- Une couleur par module : Estomac orange, Carte blanc/source, Séquenceur bleu, Ouroboros rouge-orange, FX violet, Modulation cyan, Sortie gris acier.
- La carte et l'anneau occupent le centre : ce sont les deux éléments « vivants » qui réagissent au son.

---

## 6. Limites techniques et solutions

| Limite | Risque | Solution proposée |
|---|---|---|
| **Mémoire** : sans garde-fou, 8 sources × 120 s en float + générations dépasseraient 400 Mo | RAM d'Ableton saturée | **Réservoir fixe** (voir §0) : 96 Mo par défaut, jamais dépassé, recyclage automatique. Attention : chaque instance du plugin a son propre réservoir. |
| **Taille de sauvegarde Ableton** | Projet énorme si tout l'audio est intégré | Seuls les fragments likés sont intégrés (compressés, 16 Mo max) ; fichiers glissés = simples chemins. |
| **Analyse/pitch (YIN)** coûteuse | Gel de l'UI ou glitch audio | Analyse en fond, résultat publié par swap ; la carte se remplit progressivement. |
| **Bandes de fréquences** : découpe spectrale = N fois plus de fragments | Dépassement des 512 | Plafond global + priorité aux fragments les plus énergétiques ; 4 bandes max. |
| **Croisements (vocoder, morph spectral)** : FFT lourde | Trop cher en temps réel | **Rendu en fond** puis le résultat devient un nouveau fragment ; seul le granulaire simple peut tourner en direct. |
| **Pitch shift / freq shift en direct** | CPU + latence | Pitch des fragments = resampling (gratuit). Le pitch-shift FX utilise un algorithme granulaire léger (latence ~20 ms déclarée à l'hôte) ; freq shift via Hilbert (sans latence). |
| **Limiteur lookahead + OTT** | Latence | Lookahead 1–3 ms, latence déclarée via `setLatencySamples` → Ableton compense. |
| **Boucle Ouroboros temps réel** | Rendu d'une génération = analyse + découpe de N mesures | La sortie est copiée vers un worker via FIFO ; la nouvelle génération est prête quelques centaines de ms plus tard et s'enchaîne au début de la mesure suivante (quantifiée). |
| **Réentrance/feedback** : la génération ré-avalée peut dériver vers le bruit ou le silence | Résultat inutilisable | Normalisation de chaque génération, % d'ADN réinjecté, détecteur de « bouillie » (flatness moyenne trop haute → plus d'ADN automatiquement). |
| **Drag & drop vers Ableton** | Le fichier doit exister avant le drag | Un seul fichier temporaire, effacé après ; dossier temporaire vidé à l'ouverture. |
| **MP3** | Le décodage MP3 de JUCE est désactivé par défaut (brevet/licence) | Activer `JUCE_USE_MP3AUDIOFORMAT` (OK aujourd'hui, brevets expirés). |
| **UI à 60 fps avec effets (aberration, fissures)** | CPU graphique sur Windows | Effets pré-rendus dans des images en cache, redessin limité aux zones animées ; option « UI légère ». Possibilité d'OpenGL plus tard si besoin. |
| **Licence JUCE** | Usage commercial | Licence gratuite JUCE (Personal) OK tant que le revenu est sous le seuil ; à vérifier si diffusion commerciale. VST3 SDK sous licence MIT désormais. |

**Budget CPU visé** : < 5 % d'un cœur moderne en lecture (32 voix + FX), les
pics (analyse, générations) étant sur threads de fond.

---

## 7. Plan des phases (rappel)

1. Squelette VST3 (effet + MIDI + sidechain), UI redimensionnable vide, chargement dans Ableton.
2. Estomac (capture + drag & drop) + découpe transitoires/grille + lecture simple.
3. Analyse + carte 2D + pinceau.
4. Séquenceur + règles + MIDI.
5. Boucle Ouroboros (auto/manuel, sélection, arbre).
6. Croisements, rack FX, sortie.
7. Random / cadenas / seed / macros / LFO.
8. Export, batch, presets, confort.
9. DA complète et finitions.

### Ce que tu devras installer pour la phase 1 (je détaillerai pas à pas)
- **Visual Studio 2022 Community** (gratuit) avec la charge « Développement Desktop en C++ ».
- **CMake** (inclus dans Visual Studio) et **Git** pour Windows.
- JUCE sera téléchargé automatiquement par CMake : rien à installer à la main.
