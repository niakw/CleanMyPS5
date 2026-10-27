> **Deux contrôles distincts.** La [première recette M2
> non destructive](SMOKE_TEST_PS5_13_60.md) est destinée à constater
> l'exécution réelle et les droits du titre installé. Elle ne nécessite
> pas d'avoir déjà implémenté la suppression Sony. Le présent document
> conserve les **portes de livraison finale**, qui restent bloquées.
> Ne pas transformer une recette M2 réussie en autorisation de supprimer
> des fichiers ni de convertir un jeu.

# CleanMyPS5 — portes de livraison et acceptation PS5 firmware 13.60

**État : développement natif en cours, aucune validation matérielle sur PS5 13.60.**

Ce document décrit la **validation finale**, pas une demande de test d'un prototype incomplet. Une compilation verte, un `.ffpkg` construit et un scan hôte ne prouvent ni l'exécution sur console ni la sécurité d'un nettoyage. L'application doit d'abord atteindre la parité fonctionnelle attendue.

## Livraison cible

- **Application native** `PPSA99784` avec interface VideoOut et DualSense. Le moteur d'inventaire lecture seule est déjà intégré à `eboot.bin` et fonctionne selon les permissions réellement disponibles.
- **Package `PPSA99784.ffpkg`** : homebrew UFS2 produit automatiquement en CI, destiné à un gestionnaire compatible. Ce **n'est pas un PKG Sony retail signé** ; le support de ce package par le firmware et son gestionnaire reste à valider.
- **Archive `PPSA99784.zip`** : dossier de titre alternatif si son enregistrement est pris en charge par un gestionnaire de homebrews.
- **`manifest.json`** : empreintes SHA-256 des livrables, dont le `.ffpkg`. Rejeter toute archive ou empreinte modifiée.
- **`cleanmyps5-helper.elf`** : auxiliaire de lecture et de diagnostic **facultatif** ; son lancement séparé n'est plus nécessaire pour l'inventaire de base. Le titre natif n'hérite pas automatiquement de ses privilèges.

## À terminer avant de demander la validation finale sur console

1. **Parité de navigation native :** accueil et raccourcis, états d'analyse, filtres, tri, exploration par jeu, recherche, détails et aperçu des captures. Une liste bornée de douze captures, même utile, n'est pas une galerie complète.
2. **Permissions effectives :** afficher l'étendue réelle des racines accessibles, distinguer refus/absence et ne jamais annoncer un scan complet si des chemins sont inaccessibles. Le port communautaire etaHEN 13.60 utilise une whitelist qui ne contient pas `PPSA99784` pour son déclencheur de sandbox ; ne pas utiliser ce protocole à l'aveugle.
3. **Cycle de vie :** ouverture/fermeture propre, manette, retour au menu, veille/reprise, gestion des threads et annulation des scans longs. Aucun travail en parallèle ne doit conserver un fichier ouvert sans borne.
4. **Nettoyage sûr :** trouver des objets **véritablement supprimables**, prouver les dépendances à la galerie et aux installations, effectuer des sauvegardes restaurables et implémenter un retrait transactionnel avec arrêt/reprise. Aucune suppression de sauvegardes, licences, bases actives, firmwares ou plugins chargés.
5. **Retour officiel :** conserver la liste manuelle et les avertissements ; ne jamais vendre une opération « anti-ban » ou une garantie d'absence de traces.
6. **Emballage :** verrouiller la compatibilité du gestionnaire d'installation UFS2, sa désinstallation normale et le comportement au redémarrage, sans raccourci WebKit qui se substituerait à une vraie application.

## Tests hôte et CI préalables

- Compiler le titre et le helper avec la révision épinglée, vérifier les métadonnées du titre, icône et module de runtime.
- Vérifier les empreintes SHA-256 et l'archive contre traversée de chemins, liens symboliques et fichiers dupliqués.
- Exécuter scanner/HTTP/revue médias sur arborescences synthétiques, sous sanitizer. Tester les refus d'accès, symlinks et hardlinks, ainsi que les races de chemins.
- Vérifier le handshake de compatibilité entre le titre et le helper, les timeout HTTP et le rejet des réponses incomplètes.
- Conserver un seul branchement Git `main` et un historique sans données personnelles en clair. Un audit de l'historique ne réécrit pas les anciens commits.

## Matrice d'acceptation matérielle, une fois la parité atteinte

| Domaine | Succès attendu | État |
|---|---|---|
| Installation | Package homebrew reconnu, icône et désinstallation sans manipulation de registre | **Non testé** |
| Démarrage natif | Interface TV utilisable sans navigateur ni injection manuelle d'un helper | **Non testé** |
| Contrôles | DualSense, focus et navigation, Carré pour revue, Cercle pour retour | **Non testé** |
| Analyse | Inventaire natif cohérent, couverture réelle indiquée, UI fluide pendant les scans | **Non testé** |
| Médias | Métadonnées et éventuellement prévisualisation cohérentes avec la Galerie PS5 | **Incomplet** |
| Nettoyage | Sauvegarde réellement restaurable, permission et confirmation vérifiées, espace mesuré après | **Non implémenté** |
| Retour officiel | Checklist informative, aucune garantie sur les services Sony | **Implémentation partielle** |

**Aucune modification de la console, de son jailbreak, des paramètres etaHEN ni de ses données personnelles ne doit être présentée comme un prérequis silencieux.** La phase de validation matérielle ne commencera qu'après traitement des fonctionnalités nécessaires dans le dépôt.
