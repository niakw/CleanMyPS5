# Audit technique des homebrews PS5 — décision pour CleanMyPS5

Contrôle de code effectué le 22 octobre 2026 sur des dépôts publics. **Ne pas importer leurs sources, noms, graphismes, identifiants de titres ou configurations personnelles.** Les exemples ne garantissent pas la compatibilité du firmware 13.60.

## Dépôts réellement examinés

| Dépôt | Points vérifiés dans les sources | Conséquence pour CleanMyPS5 |
|---|---|---|
| [Kylin Explorer](https://github.com/aydencharles/ps5-kylin-explorer/blob/main/src/main.cpp) | Application installée, navigation à la manette, demande de permissions via `/download0/etahen_jailbreak`; l'app attend le retrait du marqueur | Lancer un titre depuis le Shell et recevoir les droits de maintenance sont deux opérations différentes. **Le retrait du marqueur ne prouve pas à lui seul les droits effectifs** : re-sonder chaque racine en lecture seule |
| [etaHEN upstream](https://github.com/etaHEN/etaHEN) | `Allow_data_in_sandbox`, commande de jailbreak d'app, service distinct de l'interface | Ne pas déduire un droit de suppression de la seule présence d'etaHEN |
| [Port communautaire etaHEN 11.00–13.60](https://github.com/GronedWaffel/etahen-11.00-13.60/blob/main/Source%20Code/daemon/source/commands.cpp) | Le daemon recherche le marqueur dans le sandbox de l'application **uniquement après** `is_whitelisted_app(tid)`. La liste explicite inclut `ITEM00001`, `NPXS39041`, `DUMP00000`, `PKGI13337`, `PKGI12345`, `TOOL00001`, plus les identifiants contenant `LAPY` | **`PPSA99784` n'est pas dans cette liste.** Ne pas activer le mécanisme Kylin en l'état, ne pas emprunter un autre Title ID, ne pas modifier etaHEN sans consentement |
| [PS5 File Explorer](https://github.com/juma-sayeh/PS5-File-Explorer/blob/main/src/app_installer.c) | Moteur payload et installation de la tuile séparés ; diagnostics du registre et erreurs de lancement ; relance sans détruire un travail en cours | Faire du registre de titre, du serveur et des droits **trois états indépendants**. Ne pas installer silencieusement de tuile à l'exécution |
| [websrv](https://github.com/ps5-payload-dev/websrv/blob/master/README.md) | Homebrews exécutés par un serveur de payloads sous `/data/homebrew`, `/mnt/usb*/homebrew`, `/mnt/ext*/homebrew` avec `eboot.elf` | Ce chemin payload n'est **pas** le dossier natif `eboot.bin` généré par la chaîne actuelle : conserver la distinction des deux formats |
| [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv/blob/master/README.md) | Utilisation du SDK de payloads, installateur et commande spéciale de remontage `MTRW` | Ne jamais importer de remontage écriture automatique ni de capacité dangereuse dans un outil généraliste |
| [Itemzflow](https://github.com/LightningMods/Itemzflow) | Application avec frontend et daemon distincts, dépendances et privilèges spécialisés | Le découplage peut être nécessaire tant que la chaîne d'app native n'a pas de droit prouvé |
| [PS5 native app boilerplate](https://github.com/drakmor/ps5-native-app-boilerplate) | Construction d'un titre natif `eboot.bin`, `sce_sys/param.json`, runtime et variantes de packaging | Conserver la révision épinglée et valider le format généré ; un FSELF compilé n'est pas un test matériel |

## Ce qui empêche encore un seul processus installé

**Le blocage principal n'est plus la création d'un `eboot.bin` : ce build est vert en CI. C'est l'obtention fiable des permissions dans le contexte de l'application native.**

Le port communautaire etaHEN pour le firmware 13.60 publie un support de ce firmware, mais cela ne constitue **pas** une validation de son interface d'élévation pour notre titre. Plus précisément, le code de son déclencheur par fichier **ignore notre Title ID**. Le client réseau alternatif `JAILBREAK_CMD` décrit par etaHEN dépend de réglages spécifiques et doit être testé séparément : aucun code de demande de privilèges ni de modification de configuration n'est activé dans CleanMyPS5.

Pour un déploiement autonome, il faut soit :
1. une API documentée, autorisée et validée sur le stack réellement installé, avec retour de capacité vérifiable ;
2. soit un service système homebrew déjà autorisé, lancé explicitement par son environnement et auquel le frontend se connecte ;
3. soit un mode local dégradé en lecture seule, avec couverture limitée honnêtement annoncée.

L'app ne doit jamais simuler une analyse complète à partir de permissions restreintes.

## Protocole actuel et prochaine porte de validation

La communication du frontend vers le helper est restreinte au loopback. `GET /api/capabilities` est le nouveau contrôle préalable : **protocole 2**, mode `read-only`, `preflight/overview=true`, `delete/quarantine/privilegeEscalation=false`. Le frontend refuse une déclaration inconnue, périmée ou annonçant une écriture. Cela vérifie la **compatibilité**, pas une identité cryptographique authentifiée.

Le mode sans helper conserve le contrôle natif des chemins ouverts et de l'espace disponible lorsqu'il est accessible ; ce n'est pas un inventaire du SSD.

### Matrice d'acceptation sur PS5 13.60

- Démarrage de `PPSA99784` depuis le Shell après enregistrement dans un gestionnaire de titres compatible.
- Tests de la manette, sortie propre et séquences de veille/reprise.
- Lecture de `/app0`, `/download0`, `/data` et `/user/av_contents` **dans le processus titre**, en distinguant absent/refusé/listable.
- Validation du moteur auxiliaire `cleanmyps5-helper.elf`, sa version et sa frontière réseau loopback, sans réouvrir WebKit.
- Mesure du temps d'analyse. Le scan ne doit pas bloquer durablement le rendu VideoOut ; prévoir un worker de fond avec annulation avant de considérer l'expérience native terminée.
- Réalisation d'une demande de privilèges **uniquement** après identification exacte du stack, validation du protocole et consentement explicite. Recontrôle des accès après demande : pas de succès supposé.
- Vérification des APIs Galerie et des index avant toute opération sur les captures. Les sauvegardes, bases système, jeux, licences et plugins actifs restent exclus.
- Vérification de la réversibilité (sauvegarde, restauration, interruption, rollback) avant le premier endpoint d'effacement.

## Décision architecturale

Conserver **une seule branche `main`**, le titre natif comme frontend cible et le helper sans navigateur comme solution compatible lorsque nécessaire. Ne pas rebaptiser l'ELF en `eboot.bin`, ne pas copier une signature d'app, ne pas introduire le remount `MTRW`, ne pas ouvrir l'API à l'IP du LAN et ne pas affirmer que le mode « Retour officiel » prévient les bannissements PSN.

Les données personnelles du public, les journaux réels de console et les chemins de profils ne doivent jamais figurer dans ce document, dans les fixtures du dépôt ou dans les artefacts CI.
