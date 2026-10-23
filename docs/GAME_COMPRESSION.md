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

**Les dossiers d'origine ne sont ni renommés, ni écrasés, ni supprimés
automatiquement.** Les images brutes `.exfat`, `.ffpkg`, `.ffpfs` et
`.ffpfsc` restent détectées mais leur conversion individuelle via
ShadowMountPlus n'est **pas encore** câblée depuis CleanMyPS5.

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
