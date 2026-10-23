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

## Prérequis avant un vrai nettoyeur

1. Relever séparément sur PS5 13.60 les droits effectifs des trois acteurs et les chemins réellement accessibles.
2. Cartographier captures, sidecars, miniatures, bases/index actifs et WAL/SHM, leur cycle de vie et les verrous.
3. Faire fonctionner une sauvegarde, restauration et reconstitution de la Galerie sur un jeu de données connu ; vérifier l'effet visible dans la Galerie officielle.
4. Introduire *ensuite* un broker d'écriture spécifique, isolé : requêtes structurées, aucun `delete(path)` générique, allowlist, snapshot inode/device/size, validation d'identité de processus et Title ID, vérification des permissions et des fichiers actifs, sauvegarde et journal de transaction avant écriture, confirmations explicites.
5. En cas de capacité inconnue, de mutation de source, d'index actif ou de rollback impossible : **aucune écriture**.

Le protocole de NiakPS5/Eden Encore ne doit pas être copié en supposant
que sa disponibilité sur un autre titre ou firmware autorise celle du
nettoyeur. Les tests CI ne constituent pas une validation matérielle.
