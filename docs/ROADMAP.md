# CleanMyPS5 — validation et priorités

| Sous-système | Implémenté | Tests hôte | ELF compilé | Test FW 13.60 | Écritures |
|---|---|---|---|---|---|
| Interface TV / DualSense | Oui | Syntaxe JS | Oui | **À faire** | Non |
| Autodiagnostic répertoires | 15 chemins candidats prédéfinis | Fixture | Oui | **À faire** | Non |
| Scanner captures et etaHEN | Oui | Fixture | Oui | **À faire** | Non |
| Analyse intelligente coordonné (lecture seule) | Oui, cinq familles de contrôles | Tests JS + scanner | Oui | **À faire** | Non |
| Recherche et filtres médias | Oui (type, jeu, nom, date, taille) | Tests JS | Oui | **À faire** | Non |
| Carte de l'espace par jeu | Oui, sur captures connues | Tests JS | Oui | **À faire** | Non |
| Catalogue homebrews et plugins | Oui, borné/protégé | Fixture | Oui | **À faire** | Non |
| Prévisualisation JPG/PNG | Oui | HTTP et garde chemins | Oui | **À faire** | Non |
| Streaming MP4/WebM | Oui | Plages et plafonds | Oui | **À faire** | Non |
| Doublons vérifiés par octets | Borné | Comparaison et budget | Oui | **À faire** | Non |
| Diagnostic autoload etaHEN | 11 emplacements candidats | Fixture | Oui | **Chemins à confirmer** | Non |
| Volumes `statvfs` | Oui | Fixture | Oui | **À faire** | Non |
| Arrêt serveur | SIGTERM / SIGINT | Test process | Oui | **À faire** | Non |
| Export du plan | Oui, depuis l'UI | Syntaxe JS | Oui | **À faire** | Téléchargement navigateur uniquement |
| Nettoyage captures | Non | — | — | — | **Désactivé** |
| Nettoyage caches homebrew | Non | — | — | — | **Désactivé** |
| Orphelins PKG/DLC | Non | — | — | — | **Désactivé** |
| Compression / compactage | Non | — | — | — | **Désactivé** |

## Portes restantes

**P0 — UI réellement accessible** : démarrer l'ELF sur PS5, confirmer la notification, l'URL loopback et le parcours manette. L'ELF n'est pas une app installée dans le menu. Si WebKit ne permet pas cet accès, concevoir un lanceur optionnel proprement désinstallable ou un frontend homebrew autonome, sans installer de raccourci silencieusement.

**P1 — données réellement fiables** : confirmer les chemins FW 13.60, Title IDs, bases multimédias, capacités des montages, plugins et autoloaders réels.

**P2 — médias** : support JXR/HDR si décodeur licencié et testé, meilleures métadonnées par jeu/date, vitesse sur bibliothèques volumineuses, lecture vidéo avec codecs réels.

**P3 — nettoyage sûr** : APIs de suppression tenant compte de la base multimédia, export / sauvegarde, contrôles d'identité et d'intégrité, confirmation sélective, validation de l'espace réellement libéré.

**P4 — caches / PKG** : analyse de fournisseurs dédiés avec droits, chemins et API vérifiés. Ne jamais traiter un plugin actif, un jeu, une sauvegarde ou une base système comme un déchet.

P0 et P1 sont bloqués sans PS5 13.60 accessible ; compilation ELF et sanitizers ne sont pas des validations de runtime console.

## Qualité de livraison

La compilation ELF dépend dorénavant de la validation des tests natifs et des sanitizers. L'artefact fourni est accompagné de son SHA-256 et du numéro de version issu du dépôt. CodeQL examine les sources C et JavaScript via un workflow dédié ; la réussite de CodeQL n'est pas une preuve de bon fonctionnement sur la PS5.

Le jalon P0 propose aussi une variante séparée `cleanmyps5-browser.elf`, utilisant l'API de lancement du navigateur documentée par l'exemple du SDK. Elle reste **expérimentale** jusqu'au test matériel et ne change pas la variante serveur seul.

## Développement après audit outil générique

Une vue Analyse intelligente **de lecture seule** coordonne désormais l'inventaire, les contrôles de chemins, montages, etaHEN et doublons. Elle affiche quels contrôles ont réussi, les comptes et tailles à examiner, sans parler d'espace libéré. Les captures peuvent être filtrées à partir de 50 Mio ou par ancienneté de modification (90 jours), **pas** par date d'ouverture. Voir [l'audit de parité](FUNCTIONAL_PARITY_outil générique.md) pour les fonctionnalités manquantes avant nettoyage réel.

Un catalogue borné des répertoires homebrew et des fichiers de plugins etaHEN est maintenant disponible en lecture seule, avec taille nominale, compte de fichiers et limite de profondeur. Les éléments sont marqués **protégés** jusqu'à une validation croisée des registres de titres, chemins et plugins actifs. Cela ne correspond pas à un désinstalleur ni à un détecteur d'orphelins fiable.
