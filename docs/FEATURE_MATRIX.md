# CleanMyPS5 — matrice fonctionnelle

## Livraison actuelle

La version 0.10.0-analysis élargit l’inventaire aux apps inconnues et aux racines système et multimédia. Elle ajoute la couverture par racine, l’export intégral des métadonnées et le hub Outils. Les diagnostics lisant du contenu sont séparés du bilan général. [État précis et format de livraison](DELIVERY.md). Les fonctions d’effacement restent absentes. Un dossier de titre natif est maintenant produit en CI, sans validation sur PS5 13.60 ; l'application native n'embarque pas les droits du helper ELF.

## Objet et méthode

Ce document évalue uniquement les fonctions attendues de CleanMyPS5, leur avancement et leurs conditions de validation. La marque, l'interface et les fonctionnalités proposées sont indépendantes de logiciels tiers.


## Matrice vérifiable

Légende : ✅ implémenté sur hôte seulement ; 🟡 partiel / borné / lecture seule ; ❌ absent ; — non applicable ou inadapté sur PS5.

| Domaine / besoin PS5 | CleanMyPS5 | Écart vérifié et acceptation |
|---|---|---|
| Analyse intelligente : analyse coordonnée | 🟡 | **Un lancement natif** coordonne sondes d'accès, captures, homebrews, autoload et jeux/images ; L1/R1 affiche les modules séparément avec leur couverture. Doublons sur demande et compression jamais déclenchée automatiquement. Non vérifié sur PS5 13.60 |
| Analyse intelligente : recommandation et bilan | 🟡 | Nettoyage coordonne les inventaires multicatégories ; quatre pages L1/R1 séparent captures, autres catégories, volumes observés et **preuves de nettoyage manquantes** calculées par une politique native commune (propriétaire/fournisseur, liens, activité, sauvegarde/restauration, permissions). Aucune suppression ni espace certifié libérable |
| Cleanup : fichiers journaux | 🟡 | Journaux système et d’apps inconnues inventoriés, aucune purge validée |
| Cleanup : caches utilisateur/système | 🟡 | **Une troisième page DualSense « Applications » (L1/R1)** affiche les caches observés, leurs octets nominaux non additifs, leur rattachement par homonymie, leurs indices de modification et les preuves manquantes. Aucune taille n'est présentée comme un gain récupérable. Scanner natif borné : dossier `cache`, `.cache`, `tmp` ou `temp` directement sous `/data/<app>`, nombre de fichiers et octets nominaux **non additifs**. Dossiers de sauvegardes et symlinks exclus ; changements d'inode du cache, du dossier parent et des sous-dossiers imbriqués détectés par comparaison avant/après, montages distincts refusés. Si un sous-dossier est remplacé pendant l'analyse, ses octets et fichiers nominaux sont retirés du total attribué, le scan devient partiel et son horodatage ne peut pas servir à conclure à une inactivité. La nouvelle corrélation limitée à 128 noms compare exactement `/data/<app>` avec un dossier homebrew homonyme contenant un `eboot.elf` ou `eboot.bin` régulier : elle affiche le nombre de **noms identiques**, sans jamais prétendre prouver une propriété, une installation, une API fournisseur ou l'inactivité des fichiers. Les exécutables symboliques, noms sans correspondance et divergences de casse ne sont pas assimilés à une preuve. Le scanner relève aussi, sans lire le contenu, les dates de modification des dossiers et de leurs fichiers inclus : catégories **modification sous 24 h**, **date inconnue/incohérente** (notamment dans le futur, lien ou lecture partielle) et **aucune modification récente observée**. Chaque dossier compte dans une seule catégorie et les sommes ne sont pas des espaces libérables. **Une date ancienne ne prouve jamais un processus inactif**, une fermeture correcte, ni un droit d'effacer. Le contrat de sécurité du nettoyage exige désormais explicitement une **preuve indépendante de processus inactifs** pour les caches applicatifs : ni la date, ni le nom du dossier, ni un exécutable homonyme ne satisfont ce critère. Ces vérifications ne garantissent **pas** une vue atomique ni l'inactivité des fichiers ; fournisseur, usage et caractère supprimable **non attestés**. Les caches HTTP système restent protégés, aucune suppression autorisée |
| Cleanup : corbeilles | ❌ | Pas d'équivalent PS5 avéré ; à confirmer dans les mounts multimédias |
| Cleanup : captures écran & vidéos | 🟡 | Inventaire JPG/JXR/PNG, MP4/WebM, revue, photos JPG/PNG et vidéo selon codec ; pas de synchronisation base multimédia ni retrait sûr |
| Fichiers à examiner : fichiers volumineux et anciens | 🟡 | Seuils 50 / 250 Mio et filtre ≥90j par mtime ; dernier accès et autres dossiers non couverts |
| Fichiers à examiner : doublons exacts | 🟡 | Comparaison octet à octet bornée (128 images, 32 Mio/fichier, 128 Mio lectures), hardlinks exclus, pas de vidéo |
| Fichiers à examiner : images similaires | ❌ | Pas de détection perceptuelle ni fusion de quasi-doublons |
| Cleanup : fichiers temporaires d'applications | 🟡 | Repérage en lecture seule des sous-dossiers `cache`/`.cache`/`tmp`/`temp` sous `/data/<app>` ; inventaire borné à 5 000 visites, 5 profondeurs et 3 s. Les noms ne prouvent **ni la propriété, ni l'inactivité, ni l'absence de références**. Aucun retrait généralisé |
| Fichiers à examiner : téléchargements | — | Les dossiers navigateur Mac ne sont pas transposables ; détecter seulement des téléchargements PS5 réellement attestés |
| Applications : permissions des applications | ❌ | Aucun registre de permissions des homebrews vérifié sur FW 13.60 |
| Applications : éléments lancés au démarrage | 🟡 | Diagnostique certains fichiers autoload mais pas un inventaire exhaustif des processus actifs |
| Cloud Cleanup : iCloud/Drive/Dropbox | — | Aucune intégration cloud macOS sur console ; ne pas modifier les sauvegardes PS Plus |
| AI Clutter : modèles et caches IA | — | Non applicable de manière générale ; nécessite des fournisseurs homebrew particuliers |
| Informations santé/alertes de disque | 🟡 | Mesures statvfs du helper et fstatvfs natives sur les chemins accessibles. Les chemins USB/EXT ne publient plus de capacité si le périphérique n'a pas un identifiant de système de fichiers distinct de son répertoire parent : un dossier vide /mnt/usbN ne peut plus présenter à tort les Go libres du stockage interne. Ce contrôle reste conservateur (montages de même périphérique non confirmés), sans preuve d'accès au SSD complet ni somme de montages. |
| Outils rapides : hub personnel | 🟡 | Cinq raccourcis sûrs sur l’accueil (captures lourdes/anciennes, tailles égales, homebrew, stockage). Pas de favoris configurables ni de nettoyage automatique |
| Résultats après nettoyage / journal | ❌ | Aucun effacement actuellement, donc aucun rapport post-opération ni vérification d'espace libéré |
| Fichiers à examiner : recherche / groupes | 🟡 | Par jeu, taille, mtime, nom, type, recherche textuelle par chemin/jeu, pagination 36 ; pas de recherche SSD globale ni de galerie miniature |
| Carte du stockage : carte arborescente stockage | 🟡 | Statvfs 2 montages et carte des captures par jeu en barres photo/vidéo ; **nouvelle page native « Volumes observés »** dans Nettoyage (L1/R1) comparant séparément les octets nominaux des captures, apps/plugins, images de jeux et dossiers de jeux, avec couverture partielle signalée. Les périmètres peuvent se chevaucher (notamment /data/homebrew et les montages USB) : **aucune somme globale et aucun octet déclarés récupérables** ; pas de navigation SSD arbitraire |
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
| Sauvegarde / quarantaine / restauration | 🟡 | **Nouvelle copie native individuelle, à confirmer sur console** : depuis le détail d'une photo/vidéo (R2 puis deux confirmations), capsule SHA-256 d'au plus 512 Mio vers le seul dossier privé de l'application `/download0/cmp5-private-backups`, sans effacer l'original et avec relecture intégrale du fichier publié. **L2** sur le détail permet désormais, même après redémarrage, de relire en lecture seule la capsule correspondant à l'identité actuelle du fichier et de refuser toute capsule absente, tronquée ou altérée, sans modifier le disque. **L1** affiche aussi un inventaire privé borné (32 capsules / 1 024 entrées) d'identifiants anonymes, permettant de revérifier une ancienne capsule même si la source a été remplacée. Le scan reste en lecture seule et signale les résidus, fichiers suspects et couvertures partielles. Le titre compile la sauvegarde et **une restitution d'essai limitée à un second dossier privé** : depuis Archives privées → Capsules, **R2 puis deux confirmations X**, reconstruction d'une capsule `CAP_…` sous `/download0/cmp5-private-rehearsals/RAW_CAP_…` sans écrasement, avec comparaison SHA-256 et espace disponible. Ce n'est **pas une restauration de Galerie** ; aucun code ne répare les index Sony ou ne supprime un original. La sauvegarde **groupée des fichiers visibles** JPG/JXR/DAT et de la miniature située au chemin connu est maintenant compilée dans le titre natif : Carré depuis le détail puis Triangle sur les compagnons observés, deux confirmations et publication conditionnelle du manifeste `GRP_…__COMMIT` après vérification de chaque capsule. L'onglet Groupes des archives privées permet une revérification SHA-256 après redémarrage. En revanche, les associations des bases et index Sony ainsi que la **restauration complète d'un album ou d'un groupe** restent uniquement des prototypes hôte. Le titre intègre aussi la restitution **groupée et privée** : dans Archives privées → Groupes, R2 puis deux confirmations reconstruisent les membres de `GRP_…` avec marqueur de groupe et SHA-256, sans écraser le moindre fichier. **L2** revérifie après redémarrage les deux groupes privés **ou une capsule individuelle et son fichier brut** : lecture SHA-256 de chaque côté, sans aucune écriture. Ni cette restitution ni celle des capsules individuelles ne restaurent la Galerie Sony ou ses index. **R3 sur une capsule**, suivi de deux confirmations, peut retirer **uniquement** sa copie de répétition `RAW_CAP_…` déjà revérifiée par SHA-256, en conservant la sauvegarde d'origine. La taille ainsi retirée est nominale et le gain en blocs n'est pas attesté. **Si la suppression est engagée mais qu'un fsync ou une vérification finale échoue, le titre affiche « ÉTAT DE RETRAIT INCERTAIN »**, jamais un refus présenté comme sans effet. Une annulation tardive ne masque pas non plus un retrait déjà confirmé. L'effacement des données Sony et des caches système reste interdit. Ne pas considérer une capsule isolée comme une sauvegarde complète de l'album. [Contrat](VERIFIED_BACKUP.md) |
| Suppression sélective / vérification | ❌ | Pas de point d'écriture, pas de confirmation double, pas de contrôle base multimédia |
| Contrôle manette TV | 🟡 | Navigation DualSense native (quatre écrans) et interface navigateur historique ; compilation vérifiée, matériel 13.60 non testé |
| Installation dans l'accueil PS5 | 🟡 | Titre homebrew natif `PPSA99784/` compilé avec `eboot.bin` et icône. Enregistrement dans le Shell par gestionnaire compatible et lancement réel sur 13.60 à vérifier |
| Compactage / défragmentation du SSD | — | Éviter : pas de défragmentation flash, pas de gains imaginaires |
| Jeux : images exFAT / UFS / PFS volumineuses | 🟡 | **Nouveau :** écran natif DualSense et scanner borné des images `.exfat`, `.ffpkg`, `.ffpfs`, `.ffpfsc`, ainsi que des dossiers de jeu `eboot.bin` / `sce_sys`. Précontrôle individuel par moteur interne et estimation de place requise ; la conversion AMPR n'est jamais une garantie de lancement. Voir [compression des jeux](GAME_COMPRESSION.md). |
| Jeux : moteur AMPR/LZ4 intégré | 🟡 | **Packer réel inclus dans le titre natif**, service privé 6973, conversion explicite des dossiers **et images reconnues par ShadowMountPlus** avec double confirmation, destination séparée et vérification forcée. Statut consultable. **Lancement gameplay PS5 13.60 non vérifié** ; lancement des sorties issues d'images non vérifié ; génération directe de `.ffpfsc` non intégrée. |

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
