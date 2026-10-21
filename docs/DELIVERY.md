# Livraison actuelle — CleanMyPS5

## Deux livrables réellement distincts

**Titre PS5 natif expérimental :** l'archive `dist/native/PPSA99784.zip` contient un dossier de titre avec `eboot.bin`, `sce_sys/param.json`, une icône originale et le runtime requis. La compilation de ce format, ses métadonnées et ses tests hôte passent en CI. Ce n'est pas un PKG retail et **son fonctionnement sur PS5 13.60 reste non validé**.

**Moteur d'inspection :** `dist/native/cleanmyps5-helper.elf` est un payload distinct, en lecture seule, sans ouverture du navigateur, qui sert le diagnostic sur `127.0.0.1:8877`. Le frontend installé est capable de sonder directement quelques chemins de *son propre sandbox*, mais ne bénéficie pas automatiquement des privilèges du helper. Ce dernier doit actuellement être lancé séparément avec un chargeur compatible.

**Ancienne interface :** `make package` continue à fournir l'ancien `cleanmyps5.elf` avec interface WebKit, jusqu'à ce que les fonctions de consultation soient portées sur le frontend natif. C'est un produit de transition et non le format d'installation cible.

## Fonctions réellement disponibles

| Fonction | État vérifié |
|---|---|
| Interface native VideoOut, quatre écrans, manette | Code présent, compilation native CI réussie ; test DualSense sur console requis |
| Sonde native de chemins applicatifs et des emplacements /data, /user | Ouverture **lecture seule**, indication accessible / inaccessible ou absent ; les codes d'erreur ne sont pas encore différenciés côté titre |
| Diagnostic détaillé de montages et permissions | Disponible dans le helper, sous réserve de sa visibilité filesystem |
| Inventaire captures, journaux, caches, homebrews, installations | En lecture seule dans le helper ; pas encore disponible dans tous les écrans natifs |
| Recherche, filtres, aperçus, doublons, export, Return to Live détaillé | En lecture seule dans l'ancienne interface WebKit ; portage natif à faire |
| Effacement captures, caches, doublons ou résidus | **Désactivé**, aucun mécanisme d'effacement validé |
| Restauration, quarantaine, synchronisation base multimédia | Non implémentés |
| Espace récupérable | **Non calculé** : les tailles affichées sont nominales, pas des promesses de gain |

## Frontière de permission

Une application PS5 installée dans le Shell ne possède pas automatiquement un accès arbitraire à `/data`, `/user`, aux fichiers des autres applications, aux bases Galerie ou aux sauvegardes. Un ELF exécuté dans un autre contexte peut voir davantage de fichiers, mais le simple fait de les lire ou d'avoir un indice d'écriture ne signifie pas qu'ils sont supprimables sans corruption.

Le helper HTTP refuse les méthodes d'écriture. Aucun endpoint `/api/delete` opérationnel n'est distribué. Les fichiers système, comptes, sauvegardes, clés, licences, plugins actifs, bases avec WAL/SHM, inconnus et liens symboliques restent protégés.

## Correctif de sécurité 0.10.1

Le comparateur de doublons ouvre sa racine une seule fois, vérifie le descripteur avec `fstat` et le réutilise pendant l'énumération et les comparaisons. Cela empêche qu'un renommage de la racine redirige l'analyse vers un autre arbre ; les identités des fichiers sont revalidées.

Le serveur est borné en ressources, lié au loopback, et limite les routes et méthodes. Les essais d'accès ne modifient ni les permissions ni les paramètres de la console.

## Conditions indispensables avant de permettre une suppression

1. Valider la disponibilité et les privilèges des deux processus sur une vraie PS5 13.60, ainsi que le lancement via un gestionnaire de titres compatible.
2. Établir pour chaque type de fichier une API sûre, son appartenance et la dépendance éventuelle aux index de la Galerie ou du gestionnaire d'applications.
3. Ajouter une sauvegarde réelle et une restauration contrôlées, une confirmation explicite, puis des tests d'arrêt, d'interruption et de changement de fichier pendant l'opération.
4. Mesurer l'espace avant/après sans double compter les montages ni additionner hardlinks et tailles nominales.

**Compilation réussie n'équivaut pas à compatibilité PS5 démontrée.** Aucune opération sur la console ne découle automatiquement de la CI.

Voir [Port natif](NATIVE_APP.md), [Architecture](ARCHITECTURE.md), [Matrice fonctionnelle](FEATURE_MATRIX.md) et [Validation matérielle](VALIDATION_PS5_13_60.md).
