# Compression des jeux PS5 — conserver le lancement

## Recherche technique du 23 octobre 2026

Une image PS5 **`.exfat` n'est pas une archive LZ4**. La compresser
directement avec `lz4`, `zstd`, `zip` ou `7z` produit un fichier que le
chargeur ne peut pas forcément monter. La compression transparente doit
être comprise par le moteur de lecture utilisé **pendant le jeu**.

Deux voies existent :

1. **AMPR-EMU / LZ4** : les assets sont découpés en blocs et sont restitués
   à la demande. [exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr)
   fournit une application PS5 avec conversion, vérification, reprise et
   estimation de gain, nécessitant notamment kstuff et ShadowMountPlus 1.7+.
   L'auteur mentionne des essais FW 12.xx/13.xx ; le titre doit être
   compatible avec les imports `libSceAmpr`. Certains jeux, notamment
   Unity, ne démarrent pas après conversion.
2. **`.ffpfsc`** : format d'image PFS compressée montable par certaines
   versions de ShadowMountPlus. Il faut **convertir** depuis un format
   compatible et vérifier le montage ; renommer ou compresser un `.exfat`
   en LZ4 ne suffit pas. Un repli non comprimé `.exfat`/`.ffpkg` doit
   rester possible si la version compressée saccade ou ne se lance pas.

Références publiques : [exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr),
[technique AMPR-EMU](https://github.com/exFAT2AMPR/exfat2ampr/blob/main/docs/TECHNIQUE.md),
[formats d'images ShadowMountPlus](https://github.com/phantomptr/ps5upload/blob/main/FAQ.md).

## Réalisation actuelle dans CleanMyPS5

La cinquième entrée native, **JEUX ET COMPRESSION**, comprend un scanner
`src/native/game_images.c` avec les propriétés suivantes :

- Métadonnées uniquement : images aux suffixes `.exfat`, `.ffpkg`,
  `.ffpfs`, `.ffpfsc` et **dossiers de jeux homebrew** identifiés
  par `eboot.bin` et `sce_sys`. Un support exFAT est un système de
  fichiers : tous les jeux dessus n'ont pas une extension `.exfat`.
  La présence de ces noms ne prouve pas qu'un jeu soit lançable.
- Racines limitées à `/data/homebrew`, `/data/etaHEN/games`,
  `/mnt/usbN/homebrew`, `/mnt/extN/homebrew` et les deux racines de
  support pour `N=0..7`. Le sandbox du titre détermine ce qui est visible.
- Descripteurs de répertoires `openat`/`O_NOFOLLOW`, données uniquement
  via `fstatat`, sans lecture du fichier de jeu, écriture, création de
  package, appel réseau ou exécution d'un moteur externe.
- Bornes : 5 000 entrées totales, 256 par dossier et 256 inodes
  d'images distincts, profondeur maximale 6 pour le calcul de la taille
  des dossiers de jeux, budget de 3 secondes. Les liens symboliques,
  hardlinks, mutations et dépassements de bornes rendent les données
  partielles. Les dossiers trop grands sont comptés sans prétendre avoir
  obtenu leur taille exacte.
- Les images non-`.ffpfsc` de **20 Gio et plus** sont seulement « à
  examiner » ; leur taille nominale n'est pas de l'espace récupérable.
  Un jeu installé dans un dossier plutôt qu'une image n'est pas compté.

- Le titre détecte **son propre moteur embarqué**, compilé et
  inclus dans le même package, sur le port loopback **6973**. Le
  chargeur ELF 9021 est requis pour le démarrer depuis `Carré`.
- Un `GET /api/check` du moteur revalide **un dossier de jeu précis**
  et sa destination. Les réponses HTTP/JSON sont strictement bornées.
  Une compatibilité ambiguë, un avertissement ou un espace insuffisant
  empêchent la confirmation.
- Deux appuis affirmatifs `X` depuis le précontrôle favorable
  déclenchent `POST /api/start` sur le service **intégré**. Les options
  sont forcées : `verify=1`, `dedup=1`, `remove=0`,
  `overwrite=0`, `force=0`.
- Après soumission, `X` interroge `GET /api/cmp5-progress` :
  état, pourcentage, octets lus/traités et **identifiant exact du jeu
  actif**. `Triangle` peut demander une pause (point de reprise) et,
  si cette pause est confirmée, reprendre uniquement le checkpoint de
  la destination calculée pour le titre courant. Un statut appartenant
  à un autre jeu ne peut jamais autoriser pause ou reprise.
  L'interface ne prétend jamais qu'un simple travail « terminé »
  garantit le **lancement du jeu**.
  En cas d'accusé de réception manquant, **ne pas renvoyer** la
  conversion sans vérifier l'état du moteur.

**Les sources d'origine ne sont ni renommées, ni écrasées, ni
supprimées automatiquement.** Les images `.exfat`, `.ffpkg` et
`.ffpfs` peuvent maintenant être proposées au moteur AMPR si
ShadowMountPlus les reconnaît explicitement et révèle un Title ID
unique. Le moteur compare la taille et l'identité de l'image lors du
précontrôle **puis juste avant la soumission**, et refuse les réponses
ambiguës, les chemins inconnus et les `.ffpfsc` déjà compressés.
L'image originale demeure intacte. Ce traitement crée une sortie AMPR
distincte : il **ne remplace pas l'image d'origine par un `.ffpfsc`**.

## Conditions pour une option de compression réellement sûre

1. Reconnaître l'image réelle et ses métadonnées (ne pas se fonder sur
   l'extension), identifier l'installation et le jeu en cours.
2. Vérifier les versions de ShadowMountPlus, kstuff, AMPR-EMU, les imports
   du titre et le format de destination.
3. Exiger un emplacement de sortie distinct, suffisamment libre, et
   conserver l'original durant toute la conversion.
4. Vérifier les blocs et le montage dans ShadowMountPlus ; tester le
   **démarrage réel du jeu et ses chargements**, sans promettre zéro lag.
5. Ne jamais supprimer automatiquement l'original ; la suppression
   éventuelle nécessite une décision distincte après vérification.

Aucun essai de cette intégration sur la PS5 13.60 de l'utilisateur n'a
été effectué. Les tests Linux/macOS et la CI PS5 ne valident ni le
montage ni la jouabilité.

## Moteur AMPR/LZ4 embarqué — une seule application

Le package natif contient désormais **son propre ELF de compression**
(`assets/cleanmyps5-compression.elf`). Il est compilé automatiquement
à partir d'un commit source épinglé de
[exFAT2AMPR](https://github.com/exFAT2AMPR/exfat2ampr), avec des
modifications CleanMyPS5 sous GPL-3.0 :

- service **local uniquement**, port **6973** (le 6972 original est libre),
  dossier de travail isolé `/data/cleanmyps5/compression` ;
- pas de suppression source, d'écrasement, de conversion forcée, ni de
  mise à jour des fichiers de jeux automatique au démarrage ;
- sources et licences du moteur accessibles depuis sa révision GitHub
  épinglée ; NOTICE et GPL embarquées dans le package final ;
- lancement depuis **Jeux et compression → Carré** : envoi du moteur
  inclus au chargeur ELF **déjà fourni par le jailbreak** sur
  `127.0.0.1:9021`. Aucune autre application à installer ;
- la réponse locale indique uniquement qu'un service compatible répond,
  **pas** que kstuff, ShadowMountPlus ou le jeu ont été validés.

Le moteur embarqué contient bien le **véritable packer AMPR/LZ4**.
L'interface CleanMyPS5 propose désormais le **choix d'un dossier de jeu
homebrew, sa destination et le précontrôle moteur**, puis une
double confirmation pour une conversion LZ4 explicite.
Aucune compression automatique à l'installation, ni suppression
d'original n'est activée. L'application propriétaire du moteur
d'origine n'est pas requise ; seules les dépendances runtime du
jailbreak et de ShadowMountPlus restent nécessaires.

## Navigation des jeux détectés

Dans **Jeux et compression**, `L1/R1` bascule entre les statistiques et
la liste des **dossiers de jeux locaux**. Les flèches haut/bas
sélectionnent un dossier. La liste est volontairement bornée à 12
candidats lisibles et n'inclut pas de chemins saisis par l'utilisateur.
Les tailles incomplètes sont signalées ; une absence de jeu dans cette
liste n'est pas une preuve de son absence sur la console.

La sélection **ne déclenche aucune écriture**. Gauche/droite choisit
une destination dans une liste fixe, `X` demande le précontrôle
en lecture seule. Seuls les dossiers de jeux explicitement compatibles,
sans avertissement et avec espace suffisant, permettent de poursuivre.
Il faut deux validations distinctes avant l'envoi d'un travail au moteur.
Après la soumission, `X` actualise le statut ; une conversion
terminée ne garantit jamais un lancement sur console.
Le nettoyage destructeur général reste verrouillé indépendamment.

## Contrôle des doublons et suivi après fermeture

Avant **chaque POST de conversion**, le titre lit maintenant le statut du
moteur intégré. Il refuse de démarrer un autre travail si la file d'attente
n'est pas vide ou si une conversion est en cours/en pause. Un travail déjà
accepté, ou dont la réponse finale est ambiguë, reste verrouillé dans la
session : un second appui ne renvoie jamais automatiquement la demande.
Un résultat terminal appartenant au **même Title ID** impose de vérifier
manuellement la sortie plutôt que de convertir à nouveau.

**Triangle → Tâche LZ4 du moteur**, depuis le menu de compression, permet
la consultation sans modification du statut local, y compris après la
réouverture de CleanMyPS5 si le moteur tourne encore. L'écran présente
l'identifiant déclaré, le pourcentage, les octets et la file ; il ne
permet **pas** de s'approprier ou de suspendre une conversion soumise
par une autre session.

La construction du package lance également `make check` du moteur
**modifié et épinglé**. Les jeux synthétiques de ces tests vérifient la
compression/récupération LZ4, les interruptions et reprises, l'intégrité
des manifests et le maintien des sources. C'est un test hôte, pas un
essai réel sur PS5.

## Images ShadowMountPlus sans seconde application

L'onglet **Images** ajouté à CleanMyPS5 affiche une liste bornée à
12 fichiers de type `.exfat`, `.ffpkg`, `.ffpfs` ou `.ffpfsc`, avec
leur taille nominale. La liste, issue du scanner local, est **indicative**.
La conversion n'est possible qu'après une requête **lecture seule**
`GET /api/cmp5-image` adressée au moteur AMPR **déjà embarqué**.
Ce moteur demande à ShadowMountPlus les titres réellement gérés,
rejette les correspondances multiples et les images non disponibles,
et vérifie la stabilité du fichier, son type et son nombre de liens.

Le Title ID validé est transmis explicitement dans `smp=` aux API
`/api/check` puis `/api/start`, et la taille ainsi que le type
d'image sont **revalidés avant le POST**. Les protections de double
confirmation, d'espace libre, de file d'attente, de vérification
intégrale et de conservation des sources restent actives. Les images
`.ffpfsc` déjà compressées sont en lecture seule : elles ne passent
pas dans un cycle de recompression. ShadowMountPlus demeure une
**dépendance runtime du jailbreak**, pas une deuxième application
graphique à installer. Aucun montage ni démarrage effectif n'est
encore prouvé sur le matériel FW 13.60.

## Compatibilité différée et sûreté côté moteur

Le moteur exFAT2AMPR peut retourner `compatible: later` pour une image
ShadowMountPlus **identifiée mais non montée** : dans ce cas, la
compatibilité AMPR n'est pas vérifiable. CleanMyPS5 affiche maintenant
« JEU IMAGE NON MONTE — COMPATIBILITE NON PROUVEE » et **ne propose
aucun bouton de conversion**, même lorsque le Title ID et l'espace
libre sont connus. Le système doit d'abord permettre un contrôle de
compatibilité concluant ; aucune procédure de montage automatique n'a
été activée sans validation des permissions et de la réversibilité.

Au moment de mettre l'image en file, **le moteur embarqué lui-même**
redemande à ShadowMountPlus l'identité du titre et le chemin source,
refuse une image non gérée ou déjà compressée, ainsi que les fichiers
possédant des hardlinks. La distribution force indépendamment de toute
requête HTTP : `verify=true`, `remove_source=false`,
`overwrite=false`, `force=false`, `clean_leftovers=false` et
`cache_mode=0`. Le script de build vérifie ces garanties dans les
**sources C transformées**, puis lance les tests synthétiques du packer.

Ces validations ne démontrent pas encore que ShadowMountPlus et AMPR
fonctionnent ensemble sur la PS5 13.60, ni la jouabilité de la sortie.

## Durcissement des opérations de compression

Le moteur fourni dans le même titre CleanMyPS5 applique désormais des
**listes blanches de chemins** avant toute mise en file :

- Sources : un jeu directement sous `/data/homebrew`, `/data/etaHEN/games`,
  les répertoires `homebrew` / `etaHEN/games` des périphériques
  `/mnt/usb0..7` et `/mnt/ext0..7`, ou une image reconnue à la racine
  d'un de ces périphériques. Les chemins avec traversée, sous-répertoires
  arbitraires, fichiers système et sauvegardes sont refusés.
- Destinations : `/data/homebrew` et les répertoires `homebrew` des
  périphériques `usb0..7` / `ext0..7`, sous réserve que le moteur vérifie
  aussi leur présence et leur écriture effective.
- Pour une image gérée par ShadowMountPlus, l'ELF compare **de nouveau**
  l'identité du fichier *après* son propre précontrôle : périphérique,
  inode, mode, taille, date de modification et date de changement
  jusqu'à la nanoseconde, ainsi que le nombre de liens physiques.
- Les **seuls POST autorisés** par le serveur du moteur sont
  `/api/start`, `/api/pause` et `/api/resume`. Tout le reste
  est rejeté par défaut, y compris les opérations de maintenance
  héritées et les éventuelles nouvelles routes d'écriture amont.

Ces garanties sont testées en C dans la CI et vérifiées dans les
sources modifiées *réellement compilées* dans le package PS5. Elles
n'éliminent pas tous les changements concurrents possibles à
l'intérieur du système de fichiers, ni les dépendances de montage
nécessaires au fonctionnement du jeu sur console.

## Précontrôle renouvelé au moment de la soumission

La double confirmation porte maintenant sur un **plan de conversion précis**.
Avant chaque `POST /api/start`, CleanMyPS5 refait le précontrôle et
refuse de soumettre le travail si le Title ID, la taille de la source,
la taille de l'image gérée, les estimations d'espace requis ou la
capacité totale du volume de destination ont changé. Il faut alors
revenir au précontrôle et confirmer de nouveau. Une variation de
l'espace *libre* seule ne bloque pas la conversion si le nouveau
précontrôle prouve toujours la capacité suffisante.

Ce contrôle est testé par des fixtures hôte. Il ne détecte pas à lui
seul un changement de support de même capacité ni une permutation
concurrente des fichiers, qui restent sous la responsabilité des
vérifications du moteur et de la validation matérielle PS5 13.60.

## Classement de la sélection native

Lorsque le scanner trouve plus de 12 images ou de 12 dossiers de jeu,
la liste manette conserve désormais les **12 candidats prioritaires**, au
lieu des 12 premiers résultats arbitraires du système de fichiers.
Les images non compressées passent devant les `.ffpfsc` déjà compressées,
puis les fichiers les plus volumineux sont privilégiés ; les dossiers
dont la mesure est complète passent devant les mesures incomplètes.
Les égalités sont départagées par chemin pour un affichage stable.

La taille nominale n'est **pas** une estimation d'espace libérable.
Le scanner garde les totaux et signale explicitement une couverture
partielle lorsque certains candidats sont masqués. La sélection ne
donne aucune autorisation de suppression ni ne garantit la jouabilité.

En cas de plan modifié détecté après confirmation, l'écran natif affiche
explicitement **« PLAN DE COMPRESSION MODIFIÉ »** au lieu d'un échec
générique. Le retour à la sélection puis le nouveau précontrôle
imposent de refaire les deux confirmations ; aucun nouveau POST n'est
émis à partir de l'ancien accord.

## Stabilité du scan des dossiers de jeux

Les tailles de dossiers sont des mesures **indicatives en lecture seule**.
Le scanner compare maintenant pour chaque répertoire parcouru l'inode,
le périphérique, les permissions, le nombre de liens, la taille et les
horodatages modification/changement à la **nanoseconde** avant et après
son parcours. Le dossier racine du jeu est également comparé à son
entrée parent après le scan. Un changement observé entraîne une taille
signalée comme incomplète, et non un chiffre présenté comme certain.

Ces contrôles n'empêchent pas tous les changements de fichiers effectués
durant un scan : ils n'autorisent aucune écriture et ne remplacent pas
le précontrôle du moteur au moment d'une conversion.

Le harnais natif DualSense comporte également un scénario où le moteur
renvoie une **taille source différente après la double confirmation**.
Il vérifie, en traversant le vrai chemin de soumission du titre, que
l'interface signale le changement de plan et que **zéro corps de
requête POST /api/start** est émis. Le test n'utilise pas de jeu réel
et ne remplace pas la validation sur la console.

## Contrôle des chemins au moment d'admettre une conversion

En complément de la liste fermée de chemins de jeux et destinations, le
moteur vérifie désormais le chemin **sur disque** avant l'admission d'un job :
chaque composant est ouvert relativement à un descripteur de dossier avec
`O_DIRECTORY` et `O_NOFOLLOW`; les fichiers finaux doivent être réguliers,
non vides et sans lien physique ambigu, les destinations de vrais dossiers.
Cela refuse notamment les liens symboliques placés dans `homebrew/` ou dans
un répertoire parent. Des fixtures synthétiques couvrent ces cas et un faux
support USB, sans accéder aux fichiers de la console.

C'est un contrôle d'admission, **pas un verrouillage** du parcours futur par
le moteur LZ4, ni une validation du montage et lancement sous FW 13.60.
Les conversions restent explicites, sans suppression des originaux.

L'admission des conversions vérifie désormais deux fois la résolution de
la source et de la destination : au traitement de la demande, puis juste
avant la mise en file, **après** le précontrôle SMP qui peut durer longtemps.
Le vérificateur du moteur transformé exige explicitement les deux passages.
Cette double vérification réduit le risque de remplacement entre étapes,
mais ne rend pas le travail asynchrone du packer atomique : le contrôle
n'autorise ni suppression des originaux ni écriture dans les données système.

### Refus d'une substitution pendant le précontrôle

Un chemin initialement autorisé peut toujours pointer vers un **autre**
fichier ou répertoire lors de la vérification suivante. Le moteur capture
désormais les identités POSIX de la source et de la destination lors de
l'admission : périphérique, inode, type/mode, liens, taille, dates mtime et
ctime avec précision nanoseconde. Après le précontrôle, il ouvre à nouveau
les mêmes chemins composant par composant et exige la même identité.
Des tests simulent le remplacement d'une image par une autre image valide
et d'un dossier de destination par un dossier vide de même nom.
Le remplacement est refusé, même si l'ancien et le nouveau chemin sont tous
deux dans la liste autorisée. Ce contrôle reste un contrôle avant mise en
file, pas une réservation exclusive de l'emplacement pendant le pack.

### Collision avec une installation existante (images SMP)

Le moteur n'accepte une conversion d'image reconnue par ShadowMountPlus
que si le chemin `destination/TitleID` n'existe pas. Cette vérification
est ancrée sur des descripteurs avec `O_NOFOLLOW` et considère un fichier,
un dossier ou un lien symbolique (même rompu) comme une collision.
Le contrôle est effectué à l'admission, puis juste avant la mise en file.
Il concerne les **images SMP** avec Title ID vérifiable ; pour les
sources en dossiers, la protection reste actuellement celle du packer
(`overwrite=false`). Aucune vérification préalable ne garantit à elle
seule une création atomique/exclusive par le worker asynchrone.

### Nom de sortie imposé par l'identité du jeu

Le code amont acceptait `name=` dans le POST de démarrage. Un client
local aurait pu donner à la conversion un autre nom que le Title ID
contrôlé lors de l'admission, contournant ainsi la vérification de
collision. Le moteur CleanMyPS5 ignore désormais cette valeur : le
planificateur récupère le nom depuis le Title ID SMP vérifié pour les
images, ou depuis les métadonnées du jeu source pour les dossiers.
Le contrôleur de build vérifie à la fois cette neutralisation et
l'absence de l'ancien paramètre `name` dans le moteur transformé.
Cela n'autorise toujours aucun effacement d'une sortie préexistante.

### Création exclusive par le worker

Le contrôle au moment du POST et la confirmation de destination libre
ne suffisent pas à protéger les secondes qui précèdent l'exécution d'un
travail en file. Dans le moteur de conversion, la création initiale du
dossier `destination/TitleID` passe désormais par **`mkdir` sans
réutilisation** : une entrée existante, notamment un répertoire, fichier
ou lien symbolique, entraîne un échec plutôt qu'une traversée par la
création récursive de l'état de conversion. L'état de reprise n'est créé
qu'après la création du dossier neuf. La branche de reprise reste séparée.
Le contrôle des sources transformées fait échouer le build si cette
règle disparaît ; les tests amont de conversion/reprise s'exécutent dans
le workflow natif. Aucun nettoyage destructeur du SSD n'est ainsi activé.

Les tests de fiabilité Linux du moteur amont utilisent un dossier de
stockage synthétique qui n'existe pas initialement. Sous `E2A_HOST`
**uniquement**, ce parent est créé avant le `mkdir` exclusif du jeu,
afin de conserver les tests de crash/reprise. Sur PS5, la racine du
stockage doit exister et être autorisée avant toute conversion : ce
comportement de préparation hôte n'y est pas compilé.

### Annulation et erreur : conservation des sorties partielles

L'audit du worker amont a révélé deux suppressions récursives par
**chemin** en cas d'annulation ou d'échec. Un autre processus pourrait
avoir remplacé ce chemin après sa création ; la purge automatique
risquerait alors de viser un dossier étranger. Le moteur intégré
CleanMyPS5 ne procède plus à ces effacements : le contenu interrompu
est préservé pour audit et reprise. Le journal signale cette conservation.
Le vérificateur de build examine explicitement les deux branches et
refuse tout retour de `e2a_rm_rf` dans leurs handlers. Ce comportement
peut laisser un dossier partiel qui nécessite une intervention distincte,
mais **ne supprime jamais automatiquement une installation lors d'un
arrêt de conversion**. Le nettoyage de l'état temporaire d'une conversion
achevée, dans la sortie produite par le moteur, demeure distinct.

### Le checkpoint de reprise ne peut pas contourner les restrictions

La fonction `/api/resume` charge un `job_request` conservé dans un
`.e2a-state`, ce qui évitait le filtrage des champs effectué uniquement
lors du POST `/api/start`. Désormais **toutes** les entrées dans la file du
worker PS5 (nouveau job, reprise ou tentative après erreur) remettent à
zéro `remove_source`, `overwrite`, `force`, `clean_leftovers` et
`cache_mode` et imposent `verify` et `dedup`. Chaque source et destination
est contrôlée par descripteurs, le Title ID de sortie est dérivé des
métadonnées du jeu ou de l'identifiant SMP, et un ancien nom qui ne lui
correspond pas provoque un refus. Les Title IDs sont vérifiés au format
quatre lettres et cinq chiffres. Sur l'environnement hôte de test
`E2A_HOST`, les fixtures de crash/reprise historiques conservent leurs
chemins relatifs et leurs scénarios spécifiques ; le garde PS5 est
contrôlé dans le C généré avant compilation native. Cela n'établit pas
encore la sûreté de toutes les interactions avec ShadowMountPlus sur console.

### Retour de statut explicite dans le titre natif

Lorsque le moteur rapporte une conversion `error` ou `cancelled`,
l'interface DualSense indique désormais **« sortie partielle conservée »**
et avertit qu'une reprise doit être précédée d'une vérification d'état.
Une conversion interrompue ne doit jamais être présentée comme un
nettoyage réussi ni comme des octets automatiquement libérés.

Pour une image ShadowMountPlus, le contrôle au niveau de la file exige
maintenant aussi une réponse **fraîche** de SMP : source réellement gérée,
disponible, de type `image`, et dont le chemin ainsi que le Title ID
correspondent exactement au job repris. Les formats précompressés ou
inconnus sont refusés. Il ne suffit plus de fabriquer un checkpoint
avec un Title ID de forme plausible. L'absence de réponse SMP entraîne
un refus sans écriture, jamais une autorisation implicite.
