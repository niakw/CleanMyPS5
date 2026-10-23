# CleanMyPS5 — matrice fonctionnelle

## Livraison actuelle

La version 0.10.0-analysis élargit l’inventaire aux apps inconnues et aux racines système et multimédia. Elle ajoute la couverture par racine, l’export intégral des métadonnées et le hub Outils. Les diagnostics lisant du contenu sont séparés du bilan général. [État précis et format de livraison](DELIVERY.md). Les fonctions d’effacement restent absentes. Un dossier de titre natif est maintenant produit en CI, sans validation sur PS5 13.60 ; l'application native n'embarque pas les droits du helper ELF.

## Objet et méthode

Ce document évalue uniquement les fonctions attendues de CleanMyPS5, leur avancement et leurs conditions de validation. La marque, l'interface et les fonctionnalités proposées sont indépendantes de logiciels tiers.


## Matrice vérifiable

Légende : ✅ implémenté sur hôte seulement ; 🟡 partiel / borné / lecture seule ; ❌ absent ; — non applicable ou inadapté sur PS5.

| Domaine / besoin PS5 | CleanMyPS5 | Écart vérifié et acceptation |
|---|---|---|
| Analyse intelligente : analyse coordonnée | 🟡 | Orchestration lecture seule et scorecard ajoutées (inventaire, stockage, doublons, autoload et chemins). Erreurs par module visibles ; reste non validé sur PS5 |
| Analyse intelligente : recommandation et bilan | 🟡 | Bilan agrégé natif avec nombre de captures volumineuses (≥50 Mio), anciennes (mtime ≥90 jours) ; aucune suggestion certifiée ni espace réellement libéré |
| Cleanup : fichiers journaux | 🟡 | Journaux système et d’apps inconnues inventoriés, aucune purge validée |
| Cleanup : caches utilisateur/système | ❌ | Métadonnées des caches HTTP système et caches potentiels d’apps ; aucune suppression certifiée |
| Cleanup : corbeilles | ❌ | Pas d'équivalent PS5 avéré ; à confirmer dans les mounts multimédias |
| Cleanup : captures écran & vidéos | 🟡 | Inventaire JPG/JXR/PNG, MP4/WebM, revue, photos JPG/PNG et vidéo selon codec ; pas de synchronisation base multimédia ni retrait sûr |
| Fichiers à examiner : fichiers volumineux et anciens | 🟡 | Seuils 50 / 250 Mio et filtre ≥90j par mtime ; dernier accès et autres dossiers non couverts |
| Fichiers à examiner : doublons exacts | 🟡 | Comparaison octet à octet bornée (128 images, 32 Mio/fichier, 128 Mio lectures), hardlinks exclus, pas de vidéo |
| Fichiers à examiner : images similaires | ❌ | Pas de détection perceptuelle ni fusion de quasi-doublons |
| Cleanup : fichiers temporaires d'applications | ❌ | Inventaire homebrew disponible, mais pas de définition sûre de cache/résidu temporaire |
| Fichiers à examiner : téléchargements | — | Les dossiers navigateur Mac ne sont pas transposables ; détecter seulement des téléchargements PS5 réellement attestés |
| Applications : permissions des applications | ❌ | Aucun registre de permissions des homebrews vérifié sur FW 13.60 |
| Applications : éléments lancés au démarrage | 🟡 | Diagnostique certains fichiers autoload mais pas un inventaire exhaustif des processus actifs |
| Cloud Cleanup : iCloud/Drive/Dropbox | — | Aucune intégration cloud macOS sur console ; ne pas modifier les sauvegardes PS Plus |
| AI Clutter : modèles et caches IA | — | Non applicable de manière générale ; nécessite des fournisseurs homebrew particuliers |
| Informations santé/alertes de disque | 🟡 | Mesures statvfs du helper et fstatvfs natives par montage visible, sans preuve d'accès au SSD complet et sans addition des montages équivalents |
| Outils rapides : hub personnel | 🟡 | Cinq raccourcis sûrs sur l’accueil (captures lourdes/anciennes, tailles égales, homebrew, stockage). Pas de favoris configurables ni de nettoyage automatique |
| Résultats après nettoyage / journal | ❌ | Aucun effacement actuellement, donc aucun rapport post-opération ni vérification d'espace libéré |
| Fichiers à examiner : recherche / groupes | 🟡 | Par jeu, taille, mtime, nom, type, recherche textuelle par chemin/jeu, pagination 36 ; pas de recherche SSD globale ni de galerie miniature |
| Carte du stockage : carte arborescente stockage | 🟡 | Statvfs 2 montages + carte des captures par jeu en barres photo/vidéo, sélection manette ; pas de navigation arbitraire du SSD |
| Applications : désinstallation | ❌ | Pas d'API de désinstallation contrôlée / maintenance base système |
| Applications : résidus d'installations | 🟡 | Diagnostic autoload etaHEN, preflight et catalogue protégé des homebrews/plugins (taille nominale). Aucune corrélation avec registre d'installation, donc aucun orphelin avéré |
| Applications : PKG/DLC incomplets | ❌ | Aucune corrélation registre/metadata FPKG, protections inconnues |
| Applications : mises à jour | ❌ | Gestionnaire d'updates PS5 non implémenté ; ne pas modifier firmware |
| Performance : entretien et redémarrage | — | Aucune réparation système générale justifiée ; proposer des diagnostics spécialisés uniquement |
| Performance : purge DNS | — | Effacement DNS peut perturber jailbreak/résolution personnalisée ; ne pas copier comme automatisme |
| Performance : libération RAM | — | Les promesses 'RAM cleaner' sont trompeuses et sensibles sur console ; ne pas implémenter |
| Protection : malware | ❌ | Pas de base de signatures ni de moteur d'analyse fiable pour PS5 homebrew |
| Protection : confidentialité | 🟡 | Serveur en loopback, origine HTTP contrôlée, pas d'écriture ; pas d'audit des autorisations payloads |
| Mesures santé système | 🟡 | Volumes statvfs et prédiagnostic, pas de télémétrie température/CPU/GPU ou SMART validée |
| Actions rapides 'Accès rapides' | 🟡 | Navigation par cinq sections ; pas de lanceur d'outils centralisé |
| Sauvegarde / quarantaine / restauration | 🟡 | **Prototype hôte non embarqué** : copie privée SHA-256, groupes logiquement validés JXR/JPG/DAT (2 à 4 éléments de même base), puis restauration vérifiée dans un second dossier privé ; tests d'annulation, collision, corruption et marqueurs de commit. **Aucune quarantaine, restauration de Galerie, modification système ou suppression PS5** ; voir [contrat de sauvegarde](VERIFIED_BACKUP.md) |
| Suppression sélective / vérification | ❌ | Pas de point d'écriture, pas de confirmation double, pas de contrôle base multimédia |
| Contrôle manette TV | 🟡 | Navigation DualSense native (quatre écrans) et interface navigateur historique ; compilation vérifiée, matériel 13.60 non testé |
| Installation dans l'accueil PS5 | 🟡 | Titre homebrew natif `PPSA99784/` compilé avec `eboot.bin` et icône. Enregistrement dans le Shell par gestionnaire compatible et lancement réel sur 13.60 à vérifier |
| Compactage / défragmentation du SSD | — | Éviter : pas de défragmentation flash, pas de gains imaginaires |
| Jeux : images exFAT / UFS / PFS volumineuses | 🟡 | **Nouveau :** écran natif DualSense et scanner borné des fichiers `.exfat`, `.ffpkg`, `.ffpfs`, `.ffpfsc`. Aucune manipulation, estimation de gain ni garantie de lancement. Voir [compression des jeux](GAME_COMPRESSION.md). |
| Jeux : compression LZ4 jouable | ❌ | Moteurs externes AMPR-EMU + ShadowMountPlus ou `.ffpfsc` disponibles, mais **aucune conversion embarquée** ni validation du jeu sur la console. `lz4 fichier.exfat` n'est pas un format jouable. |

## Priorités, avec portes de sécurité

### P0 — Vérifications réelles, aucune écriture
1. Confirmer un seul payload démarré, son cycle de vie, l'URL loopback, la notification et la navigation DualSense sur FW 13.60.
2. Confirmer les chemins de captures et la base de métadonnées liée au menu Galerie ; confirmer si les montages sont des doublons logiques.
3. Prouver que des résultats complets ou partiels sont présentés sans déclarer faussement l'espace « libéré ».

### P1 — Parité de lecture seule utile
4. Agréger les modules dans un Analyse intelligente non destructif avec statuts et avertissements.
5. Étendre la revue Large & Old (50 Mio et âges via mtime, **pas** la vraie date de dernière ouverture).
6. Visualiser par jeu, date et volume avec filtre/tri manette ; disponibilité des aperçus JXR selon décodeur et SDK.
7. Diagnostic dédié des installations FPKG, fichiers temporaires, cache homebrews, en prouvant leurs relations avec un logiciel installé avant toute qualification.

### P2 — Nettoyage fiable, uniquement après validation P0/P1
8. Définir le contrat des éléments supprimables (autorisations, registre de capture, liens logiques, qualité des sauvegardes).
9. Sauvegarde réelle des fichiers, restauration vérifiée, mécanisme de quarantaine compatible avec l'espace libre.
10. Prévisualisation des différences, confirmation deux étapes, verrouillage en cours d'opération, revalidation des inodes et journaux d'exécution.
11. Vérifier les états et capacités avant/après via API appropriées ; traiter interruption, rollback et erreurs partielles.
12. Auditer journaux, mises à jour et fichiers système via modules **spécifiques PS5**, jamais balayage destructif générique.

## Exclusions intentionnelles
- Ne pas prétendre scanner les logiciels malveillants sans moteur ou base de signatures validés.
- Ne pas reprendre les nettoyages RAM, DNS, corbeille macOS ou défragmentation comme si la PS5 était un Mac.
- Ne pas supprimer directement `/data/etaHEN` ni la Toolbox des Paramètres : distinguer résidus, autoload et injection ShellUI.
- Ne jamais toucher aux sauvegardes, aux jeux, aux licences ni aux bases de captures sans validation spécifique.

## Définition du mot « terminé »
**Implémenté + tests hôte + compilation ELF ou titre natif ≠ validé sur PS5.** Une fonctionnalité est « prête PS5 » uniquement quand son comportement réel est vérifié sur FW 13.60, DualSense comprise. Une fonctionnalité de nettoyage est « prête à être activée » uniquement quand sauvegarde, restauration, synchronisation, consentement et tests d'interruption ont été démontrés.

| Retour au système officiel | 🟡 | Diagnostics en lecture seule, liste manuelle et export sans opérations destructives ; état PSN jamais certifié, test FW 13.60 requis |
