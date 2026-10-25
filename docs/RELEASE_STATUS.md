# CleanMyPS5 — critères de clôture (constat vérifiable)

## Helper HTTP : cadrage strict et refus des origines ambiguës (25 octobre 2026)

Le serveur de diagnostic (lecture seule, loopback) impose une ligne de
requête à trois champs exacts et les seules versions HTTP/1.0 ou 1.1.
Les caractères NUL, données/pipelines dans le même message et en-têtes
`Content-Length` / `Transfer-Encoding` sont refusés. `Host` est
unique et `Origin`, `Referer` et `Sec-Fetch-Site` ne peuvent pas
apparaître plusieurs fois, même avec des valeurs identiques. La CI
injecte de vraies requêtes TCP malformées en plus des tests navigateur
et vérifie les codes 400/403/405 ; aucune opération de nettoyage n'est
activée. C'est une réduction de surface d'attaque du helper, pas une
élévation de droits ni une preuve matérielle de la console.


## Métadonnées Galerie : refus d'accès et lecture partielle (25 octobre 2026)

Le lecteur natif des fichiers compagnons `.dat` marque désormais
la couverture **partielle** si un fichier présent pendant l'inventaire
ne peut pas être ouvert. Il assemble aussi les lectures POSIX courtes
jusqu'à la fin de la fenêtre bornée, au lieu de publier un titre
tronqué. Un EOF prématuré ou un champ de nom non terminé sur les
160 octets lus produit une couverture partielle et aucun nom de jeu.
La fixture de Galerie injecte un `EACCES`, des lectures d'un octet,
un EOF prématuré et une mutation pendant lecture. La présence des
autres captures n'est pas effacée du catalogue. Ce contrôle reste en
lecture seule et n'autorise aucune suppression.

## Comparaison de noms HTTP sans accès hors limites (25 octobre 2026)

La classification des champs des trois protocoles de lecture locale utilise
désormais une comparaison dont la taille du nom attendu est connue à la
compilation. Tout nom reçu de longueur différente est rejeté avant lecture
du littéral. Les tests incluent des noms d'en-têtes de 700 à 3 000 caractères
et s'exécutent sous AddressSanitizer/UBSan en plus des builds Linux/macOS.
C'est une protection contre un dépassement de lecture côté décodeur, pas
une preuve d'authentification du service local.

## Durcissement de la lecture HTTP locale (25 octobre 2026)

Les trois décodeurs HTTP du titre, du helper et du moteur AMPR appliquent
maintenant une validation commune de la syntaxe des noms d'en-têtes.
Les noms d'en-têtes HTTP étant insensibles à la casse, un second
`content-length`, un `CONTENT-TYPE` ou un
`transfer-encoding` déguisé ne peuvent plus contourner les contrôles
contre les longueurs ambiguës et les réponses chunked. Les champs de
cadrage reconnus restent obligatoirement dans l'orthographe attendue
des composants embarqués. Le titre refuse également toute ligne de
statut helper différente de `HTTP/1.1 200 OK`. Les tests hôte couvrent
ces variantes dans les trois protocoles. Cette validation de trames
n'authentifie pas un processus ni ne donne de droits sur la PS5.

L'application n'a **pas encore atteint la fin du développement**. Distinguer
explicitement deux objectifs évite de faire passer un build CI pour une
application PS5 validée.

## Sauvegarde privée d'une miniature externe (24 octobre 2026)

Le prototype **hôte uniquement** peut maintenant vérifier une copie
SHA-256 d'une miniature JPG.JPEG conservée sous thumbnails/photo,
au sein d'une transaction privée contenant aussi le média visible et
son fichier DAT. Quatre membres au maximum, 512 Mio au total,
vérification des identités, refus d'un groupe sans la miniature connue
et restauration strictement dans un second espace privé 0700.
Des fixtures exécutent les vraies routines de sauvegarde/restauration.

Ceci **ne prouve pas** le graphe complet de la Galerie Sony. Les
bases, index, éventuels journaux WAL/SHM et les dépendances inconnues
ne sont pas inclus ; aucune écriture en Galerie ni suppression n'est
autorisée dans le titre PS5.

## ELF intégrés : prévalidation renforcée (24 octobre 2026)

Avant toute transmission au chargeur local, le titre contrôle l'identité
x86-64, le type exécutable, la version, la taille de l'en-tête et la
table des segments des deux ELF intégrés. Une lecture partielle ou un
fichier malformé ne transmet aucun octet. Le validateur du titre et le
build du packer appliquent les mêmes contraintes, avec des lectures
d'en-tête bornées plutôt que charger tout l'exécutable en RAM. Les
fixtures injectent objets non chargeables et table de segments invalide.
Cela ne constitue pas une preuve de chargement réel sur FW 13.60.

## Helper de permissions : EPERM et EACCES (24 octobre 2026)

Le précontrôle en lecture seule du helper classe maintenant les deux
erreurs POSIX `EACCES` et `EPERM` comme « denied /
permission-denied » lors de l'ouverture de la racine, de l'examen de
l'entrée et de l'ouverture ancrée d'un dossier. Un refus `EPERM` ne
doit plus être noyé dans les erreurs « unknown ». Un test hôte injecte
`EPERM` aux trois étapes et confirme que l'inventaire ne prend
pas une autorisation manquante pour un dossier vide. Aucun droit
supplémentaire n'est demandé ou accordé par ce changement.

## Diagnostic de stockage : parcours incomplet détecté (24 octobre 2026)

La page native Accès au stockage prend maintenant en compte les échecs
de `closedir`, au même titre que les erreurs de `readdir`. La sonde
reste « listable » avec ses entrées déjà vues, mais son statut I/O
indique une **couverture partielle**, jamais une inspection complète.
Deux injections déterministes d'erreur `EIO` dans les tests de navigation
vérifient le comportement réel du scanner, sans accès à une PS5.
Si la lecture est limitée et la fermeture échoue, les deux problèmes
sont signalés, sans aucune écriture ni suppression.

## Deux livraisons distinctes

| Livraison | État du code | État de preuve | Verdict |
|---|---|---|---|
| **Diagnostic natif, lecture seule** | Interface DualSense, sondes, inventaire, catalogue, filtres, prévisualisation partielle, comparateur borné et analyse de compagnons compilés | Tests hôte et génération d'un titre homebrew ; **pas de validation matérielle sur 13.60** | **Candidat technique, non validé sur console** |
| **Nettoyeur réversible** | Prototypes SHA-256 hôte seulement ; effacement désactivé | Index multimédias, miniatures, privilèges et transaction de suppression/restauration PS5 non démontrés | **Non terminé / bloqué** |

## Cinq conditions bloquantes avant livraison fonctionnelle

1. **Installation/exécution native sur FW 13.60 :** gestionnaire de titres compatible, installation/désinstallation, démarrage sans WebKit, veille/reprise, contrôles DualSense.
2. **Permissions réelles :** vérifier pour le titre et le helper séparément chaque racine d'accès, les erreurs et limites, sans confusion entre descripteur ouvert et permission d'effacer. **Préparation logicielle intégrée :** le titre distingue refus/absence/erreur et le bilan hôte teste qu'une sonde incomplète impose une couverture partielle. Le scanner commun signale maintenant comme **partiels** les chemins trop longs, erreurs de lecture de dossiers, analyses sans aucune racine parcourue **et inventaires dont seuls les dossiers applicatifs (ou USB) sont accessibles mais aucune racine de captures photo/vidéo ne l'est**. Un résultat de zéro capture ne peut donc pas être présenté comme complet en l'absence de couverture réelle de la Galerie. Aucune preuve matérielle 13.60 n'est encore disponible.
3. **Dépendances de la Galerie :** [cartographie comparative des sources PS5](GALLERY_DEPENDENCIES.md) ajoutée : schémas divergents entre média + `.dat`, `.meta` / `.ext` et thumbnails `.jpg.jpeg` ; ces métadonnées connues mais non supportées bloquent désormais la validation d'un groupe. **Reste à prouver** sur console le lien captures → sidecars/miniatures → bases et index (WAL/SHM inclus), sans inventer de chemin ni de règle de purge.
4. **Sauvegarde/restauration système réellement réversible :** transaction vérifiée sur PS5, refus des conflits de fichiers, annulation, arrêt brutal, reconstitution des index et cohérence avant/après.
5. **Nettoyage et mesure :** critères réellement sûrs par catégorie de données, confirmation explicite, contrôle de l'espace libéré et exclusion des sauvegardes, licences, bases actives, plugins chargés.

**Aucune de ces cinq preuves matérielles ou fonctionnelles n'est acquise** du seul
fait que les builds CI passent. Le prototype de copie hôte ne compte pas comme
une restauration de la Galerie PS5.

## Ce que produit désormais chaque build natif

L'artefact de CI contient un `release-readiness.json` calculé à partir du
`manifest.json` du titre. Il n'accorde jamais automatiquement une validation
PS5 ou une autorisation de suppression :

- `nativeArtifact.status: packaged-not-console-tested` : composants présents dans le manifeste d'archive ;
- `readOnlyDiagnosticRelease.status: hardware-validation-required` : candidat de diagnostic non validé physiquement ;
- `reversibleCleanerRelease.status: blocked` : nettoyage réversible non autorisé ;
- `blockingConditions` : les cinq preuves restant à établir.

Un test automatisé rejette tout manifeste annonçant
`firmware1360HardwareValidated=true`, `cleanupEnabled=true` ou des
permissions supposées élevées. L'archive du titre reste un **homebrew non signé
retail**, et l'absence d'essai matériel reste visible.

## Méthode de clôture

Conserver **une seule branche `main`**. Prioriser les régressions qui empêchent
la stabilité, l'installation et l'analyse complète dans le sandbox réellement
disponible. Reporter les fonctions non essentielles (formats additionnels,
cosmétique, élargissement spéculatif des chemins) ; ne pas fabriquer de
« pourcentage terminé » sur des fichiers inventoriés.

La demande de validation console ne remplace ni les tests automatisés ni
l'implémentation des fonctions attendues. Aucun résultat « vert » de CI
n'autorise à publier un effaceur automatique.

## Compression des jeux — état du composant natif

Le module AMPR/LZ4 est embarqué dans un seul titre CleanMyPS5 et son
lancement utilise le chargeur ELF du jailbreak existant. Les dossiers
homebrew peuvent être précontrôlés puis soumis à une conversion **après
deux confirmations**, sans écrasement ni suppression de l'original.
Le protocole affiche également l'avancement compact du moteur.
**La compilation hôte/PS5 ne constitue pas une validation sur console
13.60 ni une garantie de démarrage d'un jeu converti.** Le nettoyage
destructeur général demeure désactivé.

La compression intégrée refuse désormais une conversion si une autre
est active ou en file, bloque la répétition d'une demande acceptée ou
ambiguë et propose un moniteur de fond accessible par Triangle.
Les tests hôte du moteur réel sont inclus dans le build de titre.

Le parcours de compression accepte désormais, en plus des dossiers,
les images de jeux reconnues par ShadowMountPlus. L'identité de chaque
image est confirmée par une API en lecture seule du moteur AMPR
intégré, puis vérifiée à nouveau avec la taille source avant création
d'une sortie distincte. Les sources `.ffpfsc` déjà compressées sont
exclues, et aucun test de gameplay PS5 13.60 n'a été effectué.

La CI vérifie désormais les protections du moteur dans le **C
transformé** avant sa compilation. Toute disparition des blocages de
suppression, d'écrasement, de forçage ou de contournement de
vérification doit faire échouer le build. Un jeu image identifié mais
non monté reste non éligible à la compression : le précontrôle
différencie explicitement l'état `later` d'une compatibilité démontrée.

### Barrières supplémentaires pour le moteur embarqué

Les sources de jeux et les destinations du moteur AMPR sont désormais
limitées aux racines connues de l'application. Le moteur refuse par
défaut toutes les requêtes HTTP POST sauf démarrage, pause et reprise
d'une compression. Il compare aussi de nouveau les métadonnées d'une
image après précontrôle pour éviter une soumission fondée sur une
identité devenue périmée. La suite native CI compile la version
transformée du moteur et vérifie ces barrières, sans pouvoir
remplacer un essai matériel PS5 13.60.


## Audit des privilèges (24/10/2026)

Le titre PS5, le helper optionnel en lecture seule et le moteur AMPR sont
**trois acteurs séparés**. Les droits d'un helper lancé par elfldr ne
sont pas transférés au titre installé ; UID/EUID et accès lecture ne
sont **pas** une autorisation de suppression. Le contrat compilé
`native/privilege_contract.hpp` distingue leurs opérations et refuse
les écritures de Galerie, caches système, installations et autoload.

NiakPS5 et Eden Encore disposent d'un élévateur ponctuel à handshake
PID/Title ID, clone des credentials, validation et rollback. Il ne faut
pas importer l'élévation générique dans un nettoyeur avant une transaction
de sauvegarde/restauration des index PS5 réellement validée. Voir
[la revue dédiée](PRIVILEGE_AUDIT.md).


### Doubles liens : aucune promesse d'espace récupérable

L'analyse des doublons exclut désormais **toutes** les entrées avec
`st_nlink != 1`, même si le second lien physique se trouve hors de
l'arborescence examinée. Elle ne déduit jamais l'espace récupérable
à partir de deux noms identiques, et signale une couverture partielle
lorsqu'elle a dû exclure des fichiers à liens multiples. Les tests de
serveur simulent deux doublons réellement indépendants et plusieurs
hardlinks, dont un vers un dossier non scanné.

### Couverture galerie : photos ET vidéos

Le scanner exige maintenant deux observations indépendantes **complètes**
des répertoires `/user/av_contents/photo` et
`/user/av_contents/video` pour déclarer la couverture de la Galerie
non partielle. Une seule famille accessible, l'autre absente ou refusée,
ne peut plus aboutir à `partial=false`, même si les répertoires
homebrew ou système sont lisibles. Une fixture vérifie le passage de
photos seules (couverture partielle) à photos + vidéos lisibles (non
partielle), puis l'effet d'un scan tronqué. Ce contrôle ne couvre pas
les index Sony ni leur restauration et n'autorise aucune suppression.

### Revue du helper intégré (24 octobre 2026)

Le helper facultatif de diagnostic, **embarqué dans le même titre natif**,
se lance manuellement depuis la page Accès et demeure un processus distinct
sans élévation de l'interface `PPSA99784`. La CI contrôle toutes ses unités
C effectivement liées et leurs en-têtes, ainsi que les fichiers copiés dans
le titre. Les compteurs retournés par le helper ne remplacent jamais les
médias réellement sélectionnables dans le titre sandboxé. Sur PS5, ses
routes GET se limitent aux quatre endpoints agrégés explicitement requis ;
aucun streaming de média ni opération de suppression ne lui est accessible.

Un scan ne peut être déclaré non partiel qu'après lecture complète de
**photos et vidéos** (les index Sony restent hors couverture). Les accès
supplémentaires du helper, un UID nul ou une validation CI ne prouvent
ni la sûreté du nettoyage ni une récupération d'espace. **Nettoyeur
réversible toujours bloqué** sans validation matérielle des transactions
et de la reconstruction des index Galerie sur PS5 13.60.

### Aperçus photo et métadonnées vidéo : identité cohérente

Les lecteurs natifs `media_preview.c` et `video_metadata.c` comparent
maintenant le **snapshot complet** produit par l'inventaire
(périphérique/inode/taille, secondes et nanosecondes `mtime` et `ctime`)
avant toute lecture. Les vérifications de stabilité existantes continuent
après lecture, y compris pour les fichiers JPEG compagnons des JXR.
Les tests de lecture refusent les snapshots artificiellement périmés à
la nanoseconde, même lorsque la taille et l'heure à la seconde sont
identiques. Cela ne représente pas une preuve de suppression/restauration
sûre des captures de la Galerie PS5.

### Tests de couverture avant packaging natif

Les lecteurs photo et vidéo partagent la même identité à la nanoseconde.
La CI Linux, macOS et sanitizers exécute maintenant une fixture photo PNG
minimaliste sans dépendance à Pillow, qui couvre lecture actuelle, refus
des snapshots périmés et liens physiques. La suite existante de lecture
MP4/WebM, avec conteneurs malformés, complète également la matrice hôte.
Les tests natifs incluant décodage JPEG/PNG/JXR et packaging PS5 restent
maintenus : aucun test hôte ne remplace une preuve matérielle 13.60.

### Autoload : diagnostics résistants au remplacement de fichier

Le lecteur natif conserve le descripteur du répertoire parent et revalide,
après la lecture, l'identité du fichier sous son **nom actuel**, et pas
seulement celle du descripteur déjà ouvert. Les contrôles comparent mode,
liens, taille, inode/périphérique ainsi que mtime et ctime à la nanoseconde.
En cas de remplacement du chemin ou d'une modification très rapide, l'état
devient « unreadable / non vérifiable », et non une référence active prouvée.
Deux courses déterministes sont exécutées sur les runners hôte et sanitizers :
renommage pendant la lecture et changement de nanoseconde avant publication.
Aucune configuration d'autoload n'est écrite par l'application.

Les diagnostics d'autoload refusent également tout fichier texte contenant un
**octet nul interne** : aucun statut « aucune référence » ne peut être
conclu en ignorant silencieusement les octets qui suivent. Une fixture
vérifie le classement « non vérifiable » de ce cas, sans modifier les
configurations de la console.

### Consultation des supports externes depuis le titre natif

La page **Accès** utilise maintenant L1/R1 pour parcourir six pages de
sondes, comprenant les racines internes ainsi que les seize montages
`/mnt/usb0…7` et `/mnt/ext0…7`. Les montages facultatifs ne sont
interrogés que par ouverture ancrée sans symlink et `fstatvfs`, sans
énumération du contenu des jeux. Le libellé **CAPACITÉ SEULE** distingue
cette mesure du nombre de fichiers réellement parcourus. L'absence
d'un support USB ne change pas la conclusion de couverture des six
sondes essentielles, en particulier la Galerie photo et vidéo.

Le helper PS5 conserve ses seules routes agrégées autorisées : l'inventaire
plus détaillé des volumes via `/api/volumes` reste une interface
diagnostique hôte. Ni ce bilan ni l'UI ne constituent une preuve d'octets
récupérables ou une autorisation d'effacement.

### Galerie : une métadonnée corrompue ne masque plus les captures suivantes

Le scanner de la Galerie conservait `rootPartial=true` pour signaler un
`.dat` corrompu, puis utilisait **ce même état** pour arrêter toute la
traversée du dossier parent. Une métadonnée illisible pouvait donc masquer
des captures saines situées après elle. Désormais, une erreur récupérable
marque bien l'inventaire **partiel**, mais le parcours borné continue :
les photos et vidéos encore lisibles demeurent consultables. Seuls une
annulation, une limite de budget ou un échec de la sortie JSON arrêtent
la traversée. La fixture hôte crée une métadonnée de trois octets,
suivie de deux photos et d'une vidéo, et exige de retrouver les trois
médias tout en conservant `cleanupValidated=false`.

### Limite de l'index des noms de jeux

Les noms extraits des fichiers de métadonnées `.dat` sont plafonnés à
**128 lectures/titres** pour préserver le temps de scan et la mémoire du
titre natif. Quand un 129e titre nouveau apparaît, le scanner **ne déclare
plus le catalogue complet à tort** : il continue à lister les captures,
mais marque la couverture du dossier `partial`. Une fixture avec 130
jeux distincts exerce ce cas, sans demander l'accès à une vraie PS5.
Ce seuil limite seulement l'association des noms, pas le nombre maximal
de médias du catalogue (10 000 sous les limites globales de scan).

### Applications/homebrews : inventaire résistant aux erreurs de dossier

Le scanner des homebrews détecte les erreurs `readdir()` (distinctes
d'une fin de dossier) et `closedir()` : ces erreurs rendent le bilan
**partiel** au lieu de le laisser faussement « complet ». Une anomalie
dans un seul sous-dossier ne masque plus automatiquement les autres
fichiers lisibles, à condition de rester dans les limites fixes de temps,
profondeur et nombre de visites. La fixture hôte injecte une erreur EIO
à la fin d'une énumération et une erreur de fermeture, vérifie le statut
partiel ainsi que la présence des applications observées. Toujours
lecture seule : aucun titre, plugin ou fichier système n'est supprimé.

### Doublons photo : observations conservées après une erreur de fichier

La comparaison des doublons sur la Galerie photo signalait déjà les
échecs de lecture/statut comme `partial`, mais arrêtait toute l'énumération
sur ce même booléen. Une image illisible pouvait donc masquer les deux
images identiques suivantes. L'explorateur continue désormais après un
échec récupérable de `fstatat` ou de sous-dossier, dans les limites fixes
(128 images, 8 000 entrées, 6 s et 128 Mio d'I/O). Il conserve `partial`
et n'autorise aucune suppression. Le test injecte `EIO` sur la **première**
image rencontrée, puis exige la comparaison exacte des deux autres,
sans supposer un ordre d'énumération du disque.

### Bilan des applications sans double comptage évident

Le rapport hôte d'applications inspectait séparément `/data/homebrew`
ainsi que `/data/etaHEN/plugins`, puis retrouvait ces mêmes dossiers
comme entrées « app-data » du parent `/data`, gonflant les tailles
nominales. Le scanner exclut désormais ces conteneurs du parent et
inspecte séparément les autres entrées de `/data/etaHEN`, sans
recompter `plugins`. Un test contrôle le total exact sur des fichiers
synthétiques et le maintien des exclusions de sauvegardes/configurations.
Les chiffres restent **nominaux et observés**, pas une quantité de stockage
récupérable ; aucune suppression ni désinstallation n'a été activée.

### Scanner de jeux : erreurs d'accès (24 octobre 2026)

Une erreur de `fstatat` pour un nom sans extension ou de `openat`
pour un dossier déjà observé peut masquer un jeu. Ces erreurs marquent
désormais le scan **partiel** sans interrompre l'inventaire des éléments
sains. L'absence normale de `eboot.bin` ou `sce_sys` ne suffit pas
à marquer tous les dossiers comme défectueux. Tests hôte déterministes
avec injection d'`EIO` et d'`EACCES`, sans écriture sur console.

### Couverture des racines essentielles d'applications (24 octobre 2026)

L'inventaire natif vérifie séparément la traversée de `/data` et de
`/user/app`. Leur absence, un refus d'accès ou une erreur ne doivent plus
produire un faux total « zéro application, scan complet » : le bilan devient
**partiel**, même lorsque des homebrews restent visibles. Le résultat JSON
retourne `rootCoverage.appData` et `rootCoverage.installedTitles` et la
page Apps et plugins affiche le diagnostic `/DATA` et `/USER/APP` sur
les deux pages. Les racines optionnelles plugins/USB peuvent légitimement
être absentes. Tests hôte : deux racines lisibles, une racine manquante et
un environnement entièrement inaccessible. Ce contrôle reste en lecture
seule et ne donne aucun droit d'effacement.

### Plus grosses installations observées (lecture seule)

L'écran natif **Apps et plugins** offre deux pages par L1/R1. La première
présente les totaux par catégorie et la seconde les **quatre éléments de
premier niveau les plus volumineux** (homebrew, plugin, données d'app ou
installation). Le scanner calcule le classement à partir des tailles
nominales de fichiers visibles dans ses budgets, en ignorant les dossiers
et fichiers privés déjà exclus. Le titre ne reçoit que quatre libellés
ASCII assainis, leur catégorie, leur taille nominale et le statut partiel,
jamais les chemins complets. Une taille ne démontre pas qu'un fichier peut
être supprimé. L'écran reste en lecture seule, sans désinstallation ni
nettoyage automatique.
