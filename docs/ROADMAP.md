# CleanMyPS5 — validation et priorités

**État natif :** dossier de titre `PPSA99784/` compilé avec `eboot.bin`, icône, runtime et écran DualSense. Le scanner de métadonnées est désormais **lié dans `eboot.bin`**, les analyses passent sur un worker avec annulation coopérative (Cercle), résultats interrompus rejetés, un catalogue natif de 10 000 médias maximum (dans la limite des dossiers et permissions) est paginé et filtrable depuis Nettoyage et le guide Retour officiel a huit étapes cochables. Un helper ELF **facultatif** expose des diagnostics et une couverture complémentaire en lecture seule sur loopback. La CI produit aussi un `.ffpkg` UFS2 expérimental. La construction active désormais une vraie boucle de rafraîchissement VideoOut, contrairement au template original qui n'affichait que l'image initiale. Compilation et tests de correspondance de version exigés en CI ; aucune validation physique sur FW 13.60 ni aucun nettoyage activé. Le tableau ci-dessous décrit majoritairement l'ancien moteur de lecture WebKit/ELF, pas la parité de l'interface native. Le scanner utilise maintenant une table de hachage exacte (dev/inode) pour éviter un coût quadratique des déduplications sur grosses bibliothèques.

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

**P0 — Titre natif et droits réellement accessibles** : vérifier ultérieurement le package `.ffpkg` ou le dossier `PPSA99784/` via un gestionnaire de titres compatible, la manette, les pages et les sondes natives **sans helper** ; la connexion loopback au helper devient facultative. Déterminer séparément les droits du titre et ceux du payload ; un titre installé ne confère pas d'élévation de privilèges.

**P1 — données réellement fiables** : confirmer les chemins FW 13.60, Title IDs, bases multimédias, capacités des montages, plugins et autoloaders réels.

**P2 — médias** : support JXR/HDR si décodeur licencié et testé, meilleures métadonnées par jeu/date, vitesse sur bibliothèques volumineuses, lecture vidéo avec codecs réels.

**Progrès vers P3 (tests hôte uniquement) :** audit non destructif des groupes privés complets, interrompus ou invalides, validation SHA-256 intégrale et détection d'une mutation du dossier durant le scan. Le contrôle de sécurité CI interdit toujours toute inclusion des fonctions de sauvegarde/reprise dans le titre natif ou le helper. Les problèmes d'index de Galerie, de restauration réelle et de privilèges PS5 restent bloquants.

**P3 — nettoyage sûr** : APIs de suppression tenant compte de la base multimédia, export / sauvegarde, contrôles d'identité et d'intégrité, confirmation sélective, validation de l'espace réellement libéré.

**P4 — caches / PKG** : analyse de fournisseurs dédiés avec droits, chemins et API vérifiés. Ne jamais traiter un plugin actif, un jeu, une sauvegarde ou une base système comme un déchet.

P0 et P1 sont bloqués sans PS5 13.60 accessible ; compilation ELF et sanitizers ne sont pas des validations de runtime console.

## Qualité de livraison

La compilation ELF dépend dorénavant de la validation des tests natifs et des sanitizers. L'artefact fourni est accompagné de son SHA-256 et du numéro de version issu du dépôt. CodeQL examine les sources C et JavaScript via un workflow dédié ; la réussite de CodeQL n'est pas une preuve de bon fonctionnement sur la PS5.

L'ancien payload WebKit reste une solution transitoire. La livraison cible est le **titre natif seul pour l'inventaire de base** ; le helper facultatif conserve les diagnostics propres à son contexte de permissions. La parité complète du frontend et les fonctions d'effacement réversible restent à développer et valider. Ne pas présenter l'archive comme un PKG Sony ni un nettoyeur terminé.

## Développement après audit outil générique

Une vue Analyse intelligente **de lecture seule** coordonne désormais l'inventaire, les contrôles de chemins, montages, etaHEN et doublons. Elle affiche quels contrôles ont réussi, les comptes et tailles à examiner, sans parler d'espace libéré. Les captures peuvent être filtrées à partir de 50 Mio ou par ancienneté de modification (90 jours), **pas** par date d'ouverture. Voir [l'audit de parité](FUNCTIONAL_PARITY_outil générique.md) pour les fonctionnalités manquantes avant nettoyage réel.

Un catalogue borné des répertoires homebrew et des fichiers de plugins etaHEN est maintenant disponible en lecture seule, avec taille nominale, compte de fichiers et limite de profondeur. Les éléments sont marqués **protégés** jusqu'à une validation croisée des registres de titres, chemins et plugins actifs. Cela ne correspond pas à un désinstalleur ni à un détecteur d'orphelins fiable.

## Return to Live

Parcours de préparation ajouté : revue des références d'autoload, état des homebrews/plugins et chemins PS5 connus, huit vérifications manuelles et export JSON. **Aucune garantie anti-ban ni détection complète du firmware/PSN.** Les opérations de réinitialisation et réinstallation restent des procédures officielles à réaliser sur la console, après sauvegarde, jamais par l'application. État : implémenté côté UI, validation FW 13.60 à effectuer.
