# Copie vérifiée et restauration : prototype hôte non destructif

## Portée exacte

`src/native/verified_backup.c` implémente un **prototype indépendant** de copie de sauvegarde de captures connues, de vérification et de restauration **dans un second dossier privé appartenant à l'application**. Ce code n'est **pas** compilé dans le titre `PPSA99784`, n'est exposé ni dans l'interface native ni par le helper, et **n'autorise aucune suppression ou modification de la Galerie PS5**.

L'objectif est d'éprouver le contrat de sauvegarde avant une éventuelle opération de maintenance, et non de prétendre que les bases de données/index multimédias PS5 sont restaurables aujourd'hui. Le moteur ne modifie jamais le fichier source.

## Propriétés testées

- Les entrées ne peuvent cibler que `/user/av_contents/photo/` ou `/user/av_contents/video/` sous une racine de **test** explicitement fournie. Parcours de chemin avec `openat` / `O_NOFOLLOW`, fichiers réguliers, sans liens physiques, et revalidation de l'identité, taille, mtime et ctime.
- Le répertoire de destination préexiste et doit appartenir au processus, sans permissions groupe/autres (mode 0700). Les identifiants de sauvegarde sont des tokens simples [A-Z0-9_-] de 1 à 48 caractères : aucun chemin arbitraire n'est accepté.
- Copie en blocs de 64 Kio, taille maximale de 512 Mio par objet. Une capsule `CMP5BAK1` contient taille et **SHA-256** du contenu, mais jamais le chemin source ou des identifiants personnels.
- La copie est d'abord écrite dans un fichier temporaire créé avec `O_EXCL`. Elle est validée avant une publication sans écrasement par `linkat`. Une sauvegarde existante ne peut pas être remplacée ; une annulation nettoie le temporaire.
- La vérification refuse capsules tronquées, modifiées, malformées, changements de fichier ou échec de SHA-256. La restauration utilise la même empreinte, écrit un nouveau fichier privé et refuse l'écrasement d'un fichier existant.
- Le test en C, lancé en CI sur Linux et macOS avec et sans sanitizers, compare l'empreinte à `hashlib.sha256`, vérifie la copie identique, le refus des liens physiques, l'annulation à mi-copie, le refus de collision, la restauration et la détection d'une capsule altérée.

## Ce qui n'est pas encore prouvé

Ce prototype ne restaure **pas** les captures dans `/user/av_contents`, ne répare aucune base d'index multimédia, ne sauvegarde pas encore de groupe atomique JXR/JPG/DAT/miniatures et n'assure pas la reprise automatique de fichiers temporaires après une panne de courant. Un fichier `.*.part` éventuellement laissé par une interruption brutale reste isolé : il n'est jamais pris pour une sauvegarde finalisée. Les garanties de durabilité en cas de panne de stockage n'ont pas été démontrées sur PS5 13.60.

**Aucune suppression, aucune quarantaine du fichier original et aucune restauration système ne doivent être activées** avant la vérification des permissions du titre, des index multimédias, d'un protocole de sauvegarde multi-fichiers, des journaux de reprise et des tests réels sur le firmware ciblé.

## Vérification

```sh
cmake -S . -B build
cmake --build build --target cmp5-verified-backup-tests
ctest --test-dir build -R verified_backup_fixture --output-on-failure
```

L'autre parcours autonome `python3 tests/test_verified_backup.py` compile le même code sans utiliser de données de la console.
