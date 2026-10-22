# CleanMyPS5 — application native PS5 et moteur de maintenance

## Deux processus, une interface installée

**L'interface utilisateur n'est plus obligée d'utiliser le navigateur.** Un premier frontend natif est compilé comme titre PS5 `PPSA99784`, avec `eboot.bin`, `sce_sys/param.json`, `sce_sys/icon0.png` et `sce_module/libc.prx`. Il utilise VideoOut et la manette pour sélectionner les quatre sections. Les opérations de lecture s'exécutent sur un worker séparé afin de maintenir les commandes et le rendu réactifs. Le moteur `inventory.c` est désormais **également lié directement à `eboot.bin`** : il peut scanner en lecture seule les racines autorisées par les permissions réelles du titre, sans avoir à lancer le helper. Sans helper, il énumère au plus **256 entrées immédiates par dossier visible** (`/download0`, `/data`, `/user/av_contents`) : ce nombre est un échantillon et **ne représente pas tout le SSD**. Il peut également consulter via `fstatvfs` l'espace disponible sur chacun des montages que son contexte parvient à ouvrir ; ces mesures ne sont jamais additionnées, car plusieurs chemins peuvent partager le même volume. Il distingue absence, refus d'accès et listing possible, sans lire les fichiers ni descendre dans les sous-dossiers. Le moteur auxiliaire reste disponible dans le payload silencieux `cleanmyps5-helper.elf` sur `127.0.0.1:8877`, mais n'est plus nécessaire à l'analyse native de base. Il peut présenter une couverture plus large selon son propre contexte d'accès.

Ce découplage est intentionnel : l'installabilité d'un titre ne lui confère pas les permissions d'un payload jailbreaké. Le frontend effectue des sondes locales en lecture seule sur `/app0`, `/download0`, `/data` et `/user/av_contents` (une ouverture réussie ou un état inaccessible/absent, sans prétendre distinguer ces deux erreurs). Dans son écran Analyse, il effectue aussi un `GET /api/preflight` via le helper, affiche les comptes accessible/refusé/absent, et propose sur **Croix** un bilan approfondi `/api/overview` (comptes par catégorie, volume nominal, caractère partiel). L'écran **Nettoyage** utilise le même bilan pour présenter les captures de **50 Mio et plus** et celles dont la date de modification dépasse **90 jours** : il s'agit de recommandations de revue, pas d'autorisations d'effacement. Ce bilan est déclenché explicitement, peut prendre plusieurs secondes et ne communique jamais de chemins privés au frontend natif. **Carré** ouvre désormais une liste native bornée des douze captures les plus volumineuses accessibles (nom affichable et métadonnées, sans chemins réels ni bouton de suppression). « Retour officiel » comporte huit étapes manuelles cochables, conservées seulement pendant la session. Le titre **ne modifie jamais de fichiers**. L'ancien frontend WebKit reste disponible pour la transition et le diagnostic.

**Statut de validation :** compilation croisée et contrôle de structure du titre sur GitHub Actions, *pas encore de lancement prouvé sur PS5 firmware 13.60*. Les chemins et les droits sont propres au firmware, au loader et au contexte d'exécution. Le format obtenu est un **dossier de titre homebrew**, pas un PKG retail signé.

## Artefacts

- `dist/native/PPSA99784.zip` : archive contenant uniquement le dossier `PPSA99784/` pour un gestionnaire enregistrant des titres homebrew.
- `dist/native/PPSA99784.ffpkg` : package UFS2 homebrew, généré par la CI avec `.NET 8` et les outils épinglés du template ; compatible seulement avec les gestionnaires qui prennent en charge ce format, non validé sur FW 13.60.
- `dist/native/cleanmyps5-helper.elf` : moteur d'inspection à lancer avec un chargeur ELF approprié ; ne lance pas le navigateur.
- `dist/native/manifest.json` : version du contrat, identifiant du titre, révision épinglée et empreintes SHA-256 exactes des fichiers produits, y compris le `.ffpkg` lorsqu'il existe. Permet de détecter un package ou helper modifié/incompatible avant transfert.
- `cleanmyps5.elf` : ancienne variante navigateur conservée tant que les fonctionnalités de l'interface native ne sont pas équivalentes.

À partir du dépôt, `make native-app` génère le dossier ZIP et le helper, ainsi que le manifeste ; `CMP5_BUILD_FFPKG=1 make native-app` génère en plus le package UFS2 si `.NET 8` est installé. Le script télécharge dans un dossier temporaire un outil de compilation natif **épinglé sur un commit précis**, remplace le code et l'icône de démonstration, compile le titre, compile le helper séparément et vérifie les fichiers produits. Aucune dépendance, image personnelle ou configuration réseau n'est introduite dans les sources suivies par Git.

## Installation de test, sans toucher au firmware

1. Extraire `PPSA99784.zip` et déposer **le dossier `PPSA99784`** dans `/data/homebrew/PPSA99784/` avec un serveur FTP déjà disponible sur la console.
2. Le gestionnaire de homebrews ou monteur de titres compatible doit déclarer ce dossier dans l'interface PS5. Copier le dossier par FTP ne garantit pas à lui seul qu'un raccourci apparaisse. Ne pas le faire passer pour un installateur PKG officiel.
3. Le titre exécute son propre inventaire **sans helper**. Le helper peut être chargé séparément plus tard si la couverture native est insuffisante ; ce n'est pas une étape d'installation obligatoire. Si un ancien `cleanmyps5.elf` tourne sur le port 8877, l'arrêter d'abord.
4. Depuis l'accueil PS5, lancer **CleanMyPS5** comme une application : Haut/Bas sélectionnent une des quatre rubriques, Croix ouvre le détail ou relance le contrôle, Cercle retourne au menu. La rubrique Stockage réalise sa propre sonde de droits même si le helper est arrêté.
5. Si `ENGINE OFFLINE OR NETWORK RESTRICTED` apparaît, vérifier le chargeur du helper, les permissions réseau loopback du titre, l'occupation du port et la notification de démarrage. Ne jamais interpréter ce message comme une autorisation de monter ou modifier `/data`.

Le développement ciblé est le firmware **13.60**, pour lequel l'archive doit encore être essayée sur une vraie console. L'intégration à un autoloader n'est pas activée par le build : le premier lancement doit être manuel. Un firmware mis à jour pourrait supprimer la compatibilité du jailbreak.

## Ce qui fonctionne dans le prototype et ce qui reste bloquant

| Fonction | Prototype natif | Validation console |
|---|---|---|
| Titre installable avec icône et `eboot.bin` | Compilé | À faire |
| Interface VideoOut et navigation DualSense | Implémentées | À faire |
| Prédiagnostic via helper local `/api/preflight` | Implémenté ; sondes de chemins natifs distinctes en lecture seule | À faire |
| Signalement d'accès refusé / absence / accès apparent | Implémenté côté moteur | À faire |
| Analyse des captures / caches dans le frontend natif | Scanner natif borné, résumé par catégorie et liste des 12 plus grosses captures visibles ; parcours exhaustif et aperçu des fichiers absents | À faire |
| Galerie, aperçu, doublons et filtres avancés dans le frontend natif | Liste sommaire des 12 médias en lecture seule ; aperçu des fichiers, recherche, groupes et doublons non portés | À faire |
| Élévation native avec réponse vérifiée | Non intégrée | — |
| Suppression de captures par API cohérente avec la galerie | Non ; seule la pré-analyse agrégée des captures anciennes/volumineuses existe dans l'interface native | — |
| Nettoyage des caches prouvés recréables et orphelins confirmés | Non | — |
| Récupération d'espace et vérification avant/après | Non | — |

## Ordre des travaux nécessaires avant d'activer le nettoyage

**1. Prouver les permissions, ne pas les supposer.** Le titre natif sandboxé et le helper doivent relever séparément les résultats `openat`, `fstatvfs`, et les erreurs par racine. Pour un éventuel mécanisme d'élévation, utiliser un protocole explicite, borné, avec confirmation de la capacité accordée et contrôles effectifs après réussite ; ne pas tenter d'accès silencieux au kernel.

**2. Produire des candidats sûrs, pas une liste de choses à effacer.** Chaque entrée exige une catégorie, une source de découverte, un statut d'application active, la vérification de son appartenance au titre, des liens physiques/associés, et un état de permission. Les chemins de sauvegardes, clés, comptes, licences, firmwares, bases actives, plugins chargés, PKG installés et montages inconnus restent exclus.

**3. Supprimer transactionnellement.** Pour les captures, trouver l'API appropriée à la galerie ou établir une méthode validée de synchronisation avec ses index avant tout effacement. Prévoir une confirmation explicite par lot, un mode prévisualisation, la possibilité de restaurer ou d'exporter, la prévention des liens symboliques, un verrou d'identité via descripteur, puis mesure `statvfs` avant/après. Si une étape n'est pas disponible, refuser la suppression.

**4. Ne pas promettre de nettoyage anti-ban.** La rubrique « Return to Live » n'est qu'un guide de préparation à des procédures officielles, jamais une garantie de non-détection ou de retour à l'état d'origine.

## Dépendances et licences

Le port natif est construit à l'aide de l'outil communautaire public `drakmor/ps5-native-app-boilerplate` à la révision épinglée `470695e0c557f99ff2df0e36e4df713c5e636526` (GPL-3.0-or-later). Le code suit GPL-3.0 ; les sources originales et notices doivent rester accessibles. Les ABI de manette et socket nécessitent encore une validation sur le firmware cible.

## Données personnelles

Le code public, les règles et la CI ne doivent pas contenir d'identifiants de profil, IP locale, e-mails personnels, captures privées ou jetons. `scripts/check_repository_hygiene.py` et `scripts/audit_privacy.py` examinent les fichiers suivis ; l'historique Git doit être audité séparément. Un résultat CI vert ne prouve pas qu'aucun secret n'a jamais été exposé. Ne jamais joindre d'export de scan contenant des chemins privés sans les caviarder.
