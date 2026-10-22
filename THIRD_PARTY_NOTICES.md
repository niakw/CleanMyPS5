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
