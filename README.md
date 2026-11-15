# IDGems Ouroboros

Recycleur de matière sonore (VST3, Windows) — 3ᵉ membre de la famille IDGems.
Architecture et maquette : [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Récupérer le plugin compilé (sans rien installer)

Le plugin est compilé automatiquement par GitHub à chaque modification.

1. Sur GitHub, onglet **Actions** → dernier passage **« Build Windows VST3 »** avec une coche verte.
2. En bas de la page, section **Artifacts** → télécharger **IDGems-Ouroboros-VST3-Windows** (un `.zip`).
3. Sur le PC, dézipper : on obtient un dossier **`IDGems Ouroboros.vst3`**.
4. Copier ce dossier entier dans `C:\Program Files\Common Files\VST3\`
   (remplacer l'ancien s'il existe, Ableton fermé).
5. Ouvrir Ableton → Préférences → **Plug-ins** → **Rescan** (VST3 activé).
6. Le plugin apparaît dans le navigateur : *Plug-ins → VST3 → IDGems → IDGems Ouroboros*.

## Compiler soi-même (optionnel)

Visual Studio 2022 + CMake :

```
cmake -S . -B build -A x64
cmake --build build --config Release --target IDGemsOuroboros_VST3
```

JUCE 9.0.5 est téléchargé automatiquement (ou `-DJUCE_DIR=chemin\vers\JUCE`).
