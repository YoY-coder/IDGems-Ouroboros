# IDGems Ouroboros

Recycleur de matière sonore (VST3, Windows) — 3ᵉ membre de la famille IDGems.
Il avale du son, le découpe en fragments, les recombine, puis ré-avale sa propre sortie
pour créer des **générations** qui mutent.

Architecture : [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## Installer le plugin (sans rien compiler)

1. Sur GitHub, onglet **Actions** → dernier passage **« Build Windows VST3 »** avec une coche verte.
2. En bas de la page, **Artifacts** → télécharger **IDGems-Ouroboros-VST3-Windows** (un `.zip`).
3. Dézipper : on obtient un dossier **`IDGems Ouroboros.vst3`**.
4. Ableton **fermé**, copier ce dossier dans `C:\Program Files\Common Files\VST3\` (remplacer l'ancien).
5. Ableton → Préférences → **Plug-ins** → **Rescan**.
6. Navigateur : *Plug-ins → VST3 → IDGems → IDGems Ouroboros*.

---

## Prise en main en 2 minutes

1. Glisse **Ouroboros** sur une piste audio (ou un retour).
2. **Nourris-le** : glisse des fichiers audio sur l'interface, **ou** fais jouer la piste puis clique **AVALER**
   (fige les dernières secondes captées).
3. Les fragments apparaissent sur la **carte**. Clique un point pour l'écouter.
4. Lance la lecture d'Ableton (ou bouton **LECTURE** dans le séquenceur) : le séquenceur pioche des fragments.
5. Clique **NOUVELLE GÉNÉRATION** : la sortie est ré-avalée, découpée, et devient la matière de la génération suivante.
6. Mets **Dry/Wet** (en bas) vers 100 % pour n'entendre qu'Ouroboros.

> Rien ne joue tant que le séquenceur est arrêté et qu'aucune note MIDI n'arrive.

---

## Les modules

| Module | Ce qu'il fait |
|---|---|
| **Estomac** (orange) | Capture continue (5–120 s) de l'entrée ou du sidechain, bouton **AVALER**, 8 sources, chargement de fichiers, dossier IDGems. Par source : mode de découpe (transitoires, grille, bandes, aléatoire), gain, poids. Clic sur la pastille = activer/désactiver la source. Jauge **Mémoire**. |
| **Carte** (blanc) | Chaque point = un fragment, placé selon 2 descripteurs au choix (brillance, bruit, hauteur, énergie, longueur). Outils : **ÉCOUTE** (clic = écouter + choisir A, Maj+clic = choisir B, **glisser un point vers Ableton** = export), **PINCEAU** / **GOMME** (zone de pioche), **LIKE**. |
| **Ouroboros** (rouge) | Générations **manuelles** ou **auto** (toutes les N mesures, lecture en cours). Mutation, ADN (part de matière d'origine), Mort (fragments non likés qui disparaissent), Gen. max. **Arbre** : clic sur un nœud pour repartir de là (nœud creux = génération fanée, audio recyclé). |
| **Séquenceur** (bleu) | Pas 1/4–1/32, 1–64 pas, swing, règles **Aléatoire / Ressemblance / Contraste**. Grille : choisis la ligne (ON, PROBA, RATCHET, REVERSE, PITCH, LONGUEUR) et dessine. Clic droit sur un pas = remise à zéro. **CROISER** : croise les fragments A et B (enveloppe, granulaire, morph, collage). |
| **MIDI** | Route une piste MIDI vers Ouroboros. *Clavier = carte* : la note choisit la zone de la carte (horizontal), la vélocité la hauteur. *Chromatique* : la note transpose. |
| **FX Rack** (violet) | Crush, Dist, Filtre, Shift (pitch/fréquence), Stutter, Delay, Reverb. Bouton = activer. **Glisse la poignée ≡** pour changer l'ordre. |
| **Modulation** (cyan) | 4 LFO à **dessiner au crayon** (sync, stéréo), 2 random (S&H ou lissé), **CHAOS** + 8 macros. |
| **Sortie** (gris) | OTT → soft clip → limiteur, passe-haut, grave mono, Dry/Wet, gain, vu-mètres, **PANIC**, **EXPORT**, zone « glisse la génération vers Ableton ». |

### Clic droit sur n'importe quel potard
Cadenas (exclu du random) · Plage de random · **Moduler par** (LFO, RND, macro, profondeur) · **MIDI Learn** · Valeur par défaut.

### Random
**RANDOM** (tout) ou bouton **DE** de chaque module · **Dosage** · bouton **LOCK** de module · **seed** (clic = rejouer / taper une seed) · **HIST.** = revenir à l'état d'avant un random, favoris.

### Raccourcis clavier
| Touche | Action |
|---|---|
| R | Random général |
| G | Nouvelle génération |
| S | Avaler |
| X | Croiser A × B |
| P | Panic |
| B | Basculer A / B |
| Ctrl+Z / Ctrl+Y | Annuler / Rétablir |

---

## Mémoire et fichiers : les garanties

- Chaque instance a un **budget mémoire fixe** (48 / 96 / 192 Mo, réglable dans l'Estomac), **jamais dépassé**.
  Quand il est plein, les plus vieilles générations « fanent » (leur audio est libéré).
- 60 s maximum par fichier chargé, capture en buffer circulaire (ne grossit jamais).
- **Aucun fichier n'est écrit sans action de ta part** : seuls EXPORT, Batch, le glisser vers Ableton
  et la sauvegarde de presets écrivent sur le disque, dans `Documents\IDGems Ouroboros\`.
- Le projet Ableton garde les réglages, les chemins des fichiers et **seulement les fragments likés** (≤ 16 Mo).

## Exports
WAV 24 bits (tempo dans le nom) + fichier **JSON** de métadonnées à côté (format `idgems-fragment-v1`,
prévu pour le futur pont avec IDGems Synth). Dossier par défaut : `Documents\IDGems Ouroboros\Exports`.

---

## Compiler soi-même (optionnel)

```
cmake -S . -B build -A x64
cmake --build build --config Release --target IDGemsOuroboros_VST3
```

JUCE 9.0.5 est téléchargé automatiquement (ou `-DJUCE_DIR=chemin\vers\JUCE`).
