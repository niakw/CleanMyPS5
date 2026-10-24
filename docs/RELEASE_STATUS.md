# CleanMyPS5 — critères de clôture (constat vérifiable)

L'application n'a **pas encore atteint la fin du développement**. Distinguer
explicitement deux objectifs évite de faire passer un build CI pour une
application PS5 validée.

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
