# Licences et dépendances tierces

CleanMyPS5 est publié sous **GPL-3.0-or-later** (voir `LICENSE`).
Les bibliothèques listées ci-dessous ne changent pas la licence du code propre à CleanMyPS5.

## Décodeur JPG/PNG

- Projet : `stb_image.h` de **Sean Barrett et contributeurs** ([nothings/stb](https://github.com/nothings/stb)).
- Révision de source épinglée : `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d`.
- Licence du fichier : domaine public **ou** MIT, au choix. CleanMyPS5 utilise les conditions MIT.
- Construction : clonage temporaire, vérification de l'identité du commit, puis compilation dans l'application native ; le dépôt source tiers n'est pas incorporé au Git du projet.
- Paramètres : JPEG/PNG seulement, accès mémoire uniquement, aucune écriture et aucune connexion réseau.

Le texte de la licence MIT utilisé par cette bibliothèque :

> Copyright (c) 2017 Sean Barrett
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
> THE SOFTWARE.

Les autres outils et le runtime de construction sont décrits dans `docs/PS5_HOMEBREW_TECH_AUDIT.md`. Les dépendances conservent leurs licences respectives.

## Compression des jeux PS5 — moteur incorporé

Le moteur embarqué dans le titre est une **version modifiée** des sources
[exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr) :
**exFAT2AMPR – Copyright (C) 2026 V-10. Créé par V-10.**
Les modifications CleanMyPS5 sont réalisées en 2026 par les
contributeurs CleanMyPS5. Cette version, renommée
**CleanMyPS5 Compression Engine**, n'est pas l'œuvre originale
et n'est pas soutenue ni approuvée par V-10.

Le code est GPL-3.0-or-later avec les **conditions supplémentaires GPLv3
section 7** énoncées dans le fichier upstream `NOTICE`. Le package
distribue `assets/compression-upstream.NOTICE`,
`assets/compression-GPL-3.COPYING` et le commit source exact
`6b3a76a12e504c196848d49e2676047fc2a8f5d4`.
Sont également crédités DLPSGAME (format LZ4), drakmor (ampr_emu et
ShadowMountPlus), Yann Collet (LZ4, BSD-2-Clause).
La licence de l'application et les notices tierces restent conservées.
