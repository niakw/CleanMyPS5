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

- La page effectue une **requête GET** vers
  `http://127.0.0.1:6972/api/hello` pour repérer le service
  exFAT2AMPR local. Elle refuse un cadrage HTTP erroné et les champs
  JSON inattendus. La réponse est un **indice de compatibilité**,
  pas une authentification du processus, ni une preuve du montage du jeu.
  Aucun jeton, appairage, chemin de jeu ou ordre de conversion n'est envoyé.

**Aucun bouton « Compresser maintenant » ne déclenche d'écriture.**
L'outil de conversion externe dispose déjà de ses propres interfaces
avec choix de destination, compatibilité, vérification des blocs et
reprise. CleanMyPS5 ne se substitue pas aujourd'hui à cette chaîne.

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
L'interface CleanMyPS5 ne propose **pas encore** le choix des jeux et
le déclenchement de la conversion (prochaine étape). Aucune conversion
automatique ni suppression d'original n'est activée.

## Navigation des jeux détectés

Dans **Jeux et compression**, `L1/R1` bascule entre les statistiques et
la liste des **dossiers de jeux locaux**. Les flèches haut/bas
sélectionnent un dossier. La liste est volontairement bornée à 12
candidats lisibles et n'inclut pas de chemins saisis par l'utilisateur.
Les tailles incomplètes sont signalées ; une absence de jeu dans cette
liste n'est pas une preuve de son absence sur la console.

La sélection **ne déclenche aucune écriture** : le précontrôle individuel
du jeu, le choix de destination, la confirmation et la conversion sont
des étapes distinctes à relier et valider. Le moteur ELF **est** bien
embarqué, mais « moteur présent » ne signifie pas « jeu compressé ».
