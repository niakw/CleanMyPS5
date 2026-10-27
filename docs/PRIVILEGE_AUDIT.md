## Processus : diagnostic à la demande sans droit d'effacement (27 octobre 2026)

La cinquième page de **Applications et plugins** présente une sonde
**en lecture seule, lancée seulement sur X**. Sur les environnements
FreeBSD disposant de `sysctl(KERN_PROC_PROC)`, elle effectue deux
requêtes (taille et instantané), limite les octets à 512 KiB et
l'analyse à 1 024 enregistrements, vérifie la disposition et les tailles
des `kinfo_proc`, puis compte les processus visibles et les noms
indicatifs `eboot.bin` / `*.elf`. **Aucun PID ni nom brut n'est
conservé ou transmis**.

Si la sandbox du titre refuse `sysctl`, si la structure est
malformée, si la liste a changé au point de dépasser la limite ou si
le SDK n'expose pas cette API, le bilan reste **non vérifié**.
Le simple nom `eboot.bin` ne suffit pas à identifier un jeu,
un propriétaire de cache ou le processus d'une application donnée.
Un instantané sans processus de jeu ne constitue donc jamais une
preuve d'inactivité. Toute suppression reste interdite.

Le chemin d'observation est compilé conditionnellement selon les
en-têtes/plateformes disponibles. Les tests hôtes injectent des
enregistrements factices, des échecs de requête, des entrées
tronquées/malformées et le dépassement des limites. La navigation
DualSense vérifie que le résultat `unavailable` demeure un
bloqueur, pas un état `idle`. Une **validation du sysctl dans le
contexte du titre sur firmware 13.60** reste obligatoire avant
d'utiliser ces compteurs pour établir des règles ciblées.

# Audit de frontière des privilèges : CleanMyPS5

## Comparatif du 24 octobre 2026

- [NiakPS5](https://github.com/niakw/NiakPS5/tree/main/native/title/elevation) utilise un helper ponctuel pour **son propre titre** et son installation, et vérifie l'identité du processus ; pas de service résident permanent.
- [Eden Encore](https://github.com/niakw/Prospero.Eden-Encore/tree/dev/ps5-sparse-jit/headless/elevation) utilise un handshake borné, PID/Title ID précis, clone de credentials, vérification de l'état, rollback et arrêt si l'état après échec est ambigu.
- **CleanMyPS5** : titre `PPSA99784` sous sandbox, helper facultatif `GET` à 8877 pour diagnostics uniquement, moteur AMPR embarqué à 6973 pour les seules conversions explicites. Les permissions ne se transfèrent pas entre processus.

## Opérations permises par acteur (contrat réellement testé)

| Opération | Titre | Helper | AMPR |
|---|---|---|---|
| Lire / inventorier les chemins accessibles | Oui | Oui | Non par ce contrat |
| Demander une compression après précontrôle | Oui | Non | Non |
| Réaliser la conversion AMPR vérifiée | Non | Non | Oui |
| Effacer Galerie / cache système / PKG | Non | Non | Non |
| Modifier autoload / élever les credentials | Non | Non | Non |
| Restaurer une Galerie avec index Sony | Non | Non | Non |

La lecture d'un chemin, `faccessat(W_OK)`, un UID/EUID zéro ou l'état
d'un **autre** processus ne valent pas validation de suppression.
Le titre affiche UID/EUID à titre informatif, jamais comme preuve de droits.
Les copies/restaurations SHA-256 du dépôt sont **hôte uniquement**.

### Diagnostic des montages non vérifiables (24 octobre 2026)

Un accès réussi au dossier n'implique pas que l'on connaît l'état
de son système de fichiers. Si `fstatvfs` échoue, le helper émet
`mount-unverified` et `mountInfoKnown:false`, conserve son
`writableHint:false` et incrémente `mountInfoUnknown` dans le résumé.
Un test simule un échec de `fstatvfs` alors que `faccessat` aurait
annoncé l'écriture autorisée : aucune tentative d'obtenir cet
indicateur n'est alors faite. Ces données sont purement diagnostiques
et ne donnent jamais de droit de suppression.

Le décodeur natif lit maintenant `mountInfoUnknown` avec une borne
cohérente (au maximum le nombre de racines accessibles), refuse les
doublons et continue d'accepter les diagnostics plus anciens en les
affichant comme **« montages non détaillés »**. Dans la vue Analyse,
les montages dont la lecture des attributs échoue sont donc visibles,
sans transformer ce signal en droit d'écriture.

### Cohérence des droits entre lignes et résumé (24 octobre 2026)

La réponse du helper est vérifiée ligne par ligne : chemin ancré,
statut, raison, flags de lecture seule et informations de montage.
Le nombre de chemins accessibles, refusés, absents ou autres, et les
montages indéterminés, doivent concorder avec le résumé. Les
contradictions, les chemins en double et les composants `.`, `..` ou
vides sont rejetés sans publier de compteurs. Le décodeur
est sans allocation et borné à 64 entrées, et garde le support des
anciens helpers ne fournissant pas `mountInfoKnown`. La conformité
d'une réponse n'authentifie pas le processus loopback : aucune
capacité d'écriture n'est ainsi accordée.

## Prérequis avant un vrai nettoyeur

1. Relever séparément sur PS5 13.60 les droits effectifs des trois acteurs et les chemins réellement accessibles.
2. Cartographier captures, sidecars, miniatures, bases/index actifs et WAL/SHM, leur cycle de vie et les verrous.
3. Faire fonctionner une sauvegarde, restauration et reconstitution de la Galerie sur un jeu de données connu ; vérifier l'effet visible dans la Galerie officielle.
4. Introduire *ensuite* un broker d'écriture spécifique, isolé : requêtes structurées, aucun `delete(path)` générique, allowlist, snapshot inode/device/size, validation d'identité de processus et Title ID, vérification des permissions et des fichiers actifs, sauvegarde et journal de transaction avant écriture, confirmations explicites.
5. En cas de capacité inconnue, de mutation de source, d'index actif ou de rollback impossible : **aucune écriture**.

Le protocole de NiakPS5/Eden Encore ne doit pas être copié en supposant
que sa disponibilité sur un autre titre ou firmware autorise celle du
nettoyeur. Les tests CI ne constituent pas une validation matérielle.

### Contrôle CI des surfaces d'écriture natives

Les neuf scanners C copiés par le script de build dans le titre ainsi que
l'interface, le décodeur et le helper doivent rester exempts de primitives
d'écriture système, de modification d'UID/GID et de suppression directe.
Les ajouts de sources natifs non approuvés échouent avant le build SDK.
Tests par injection de `unlinkat`, `rename`, `O_CREAT`, `seteuid` et
une unité de sauvegarde illégitime. Contrôle structurel seulement : il ne
remplace pas une revue d'appel indirect ni un test sur console.

### Permissions de photo et vidéo : sondes séparées

La vue Accès propose six sondes, dont `/user/av_contents/photo` et
`/user/av_contents/video` séparées du parent. L1/R1 permet de passer
entre les deux pages ; un sous-dossier refusé rend le bilan **partiel**.
L'accès aux miniatures et aux index de la Galerie ne se déduit pas de
l'ouverture réussie de ces répertoires.

### Contrôle récursif du code natif (24 octobre 2026)

L'audit automatisé parcourt maintenant tous les en-têtes locaux
atteignables depuis le titre et les scanners natifs réellement compilés.
Un nouveau fichier inclus mais absent de la liste de staging PS5 est refusé.
Les tests injectent également des appels d'écriture indirects via un
faux en-tête, ainsi que `linkat`, `symlinkat`, `fchmodat`, `O_TMPFILE`,
`posix_spawn`, appels système bruts et primitives kernel de mutation.

La décision demeure : pas d'élévation globale du titre. Une éventuelle
écriture de Galerie exigera un broker dédié, authentifié sur PID/Title ID,
avec transactions prouvées sur console, pas la copie d'une capacité
NiakPS5 ou Eden Encore. Ce contrôle statique n'est pas une preuve de
sécurité absolue ni une validation matérielle.

### Liste blanche des unités de compilation

Le contrôle de packaging refuse dorénavant toute unité supplémentaire
`.c`, `.cc`, `.cpp` ou `.cxx` et toute substitution/renommage
d'une unité source dans le titre natif. Seuls les neuf scanners de lecture
révisés, `native/main.cpp` et `native/preview_decode.c` peuvent être
copiés dans le modèle de compilation épinglé. Les tests injectent une
fausse implémentation d'élévation ou de nettoyage pour vérifier le refus.

### Séparation stricte des résultats du titre et du helper

Le scanner du titre et celui du helper disposent potentiellement de
droits différents. Le helper ne peut désormais plus **remplacer** les
compteurs du titre lorsque celui-ci trouve des résultats partiels. Son
résultat est conservé dans une structure indépendante, présenté sous le
libellé **« agrégats helper uniquement »** ou comme une observation
séparée. Il ne fournit jamais de chemin de sélection natif, n'augmente
pas le nombre de médias sélectionnables et ne déverrouille aucune
suppression. Un test natif injecte volontairement 120 fichiers côté
helper tout en confirmant que le catalogue du titre reste identique.

### Helper inclus sans second titre ni élévation globale

Le titre contient maintenant le helper ELF en lecture seule. La page Accès
permet de l'envoyer **manuellement avec Triangle** au chargeur de payload
local existant, sans installation d'une seconde application. Le helper
dispose de son propre processus ; le titre ne copie jamais ses droits.
Un envoi réussi n'est pas présenté comme un démarrage confirmé : X permet
ensuite de relancer les sondes et vérifier la réponse du helper.
Le mode reste lecture seule, sans suppression de captures ou de caches.
L'exécution effective sur PS5 13.60 devra être vérifiée sur console.

### Compilation effective du helper vérifiée

Le contrôle de la surface de privilèges examine maintenant aussi **les neuf
unités C réellement liées dans l'ELF de diagnostic** par le `Makefile`,
ainsi que leurs en-têtes locaux récursifs, et non seulement `server.c`.
L'audit bloque les ajouts de sources comme un module d'écriture non déclaré,
ainsi que toute modification de la ligne de compilation du helper qui
contournerait la variable `NATIVE` contrôlée. Les unités supplémentaires
`media.c`, `video.c`, `volumes.c` et `preflight.c` sont comprises.
Ces contrôles sont des protections structurelles de CI, non une preuve
d'exécution ou de droits suffisants sur la PS5 13.60.

### En-têtes et routes HTTP PS5 réellement accessibles

Le diagnostic embarqué sur **PS5** accepte seulement les requêtes GET
`/api/status`, `/api/capabilities`, `/api/preflight` et
`/api/overview`. Il refuse les routes de streaming vidéo et image,
le rapport détaillé de fichiers, les chemins de plugins, les fichiers
autoload et toute route inconnue. L'interface native n'utilise aucun
de ces chemins détaillés du helper. Le serveur hôte de développement
conserve les prévisualisations pour ses fixtures synthétiques.

**Limite connue :** le port loopback demeure non authentifié et les
résumés agrégés restent visibles aux processus locaux capables d'y accéder.
Il serait incorrect d'y ajouter de la suppression ou un broker de droits
tant qu'une authentification forte du titre appelant n'est pas en place.

### Séparation du mode helper et du serveur navigateur historique

Le **helper empaqueté** est compilé avec `-DCMP5_DIAGNOSTIC_HELPER`.
Les quatre routes HTTP agrégées du diagnostic sont uniquement exposées
dans ce mode ; les routes de prévisualisation restent associées au
serveur historique de développement sur ordinateur ou à l'ancien payload
navigateur `ps5`, qui n'est **pas** lancé par le titre natif.
Un garde compilateur refuse de combiner le mode helper avec
`CMP5_AUTO_BROWSER` : pas de WebKit ou de notification d'interface
navigateur lorsque l'utilisateur démarre le helper de diagnostic.
Le contrôle CI exige le drapeau de compilation séparé, vérifie les
routes et simule sa disparition pour empêcher la régression. Cette
séparation ne constitue pas une authentification du service loopback.
