# Livraison actuelle — CleanMyPS5

**Statut de clôture vérifiable :** voir [diagnostic natif et cinq conditions bloquantes](RELEASE_STATUS.md). Un titre compilé n'est pas une application validée sur PS5 13.60.

## Deux livrables réellement distincts

**Titre PS5 natif expérimental :** l'archive `dist/native/PPSA99784.zip` contient un dossier de titre avec `eboot.bin`, `sce_sys/param.json`, une icône originale et le runtime requis. La compilation de ce format, ses métadonnées et ses tests hôte passent en CI. Ce n'est pas un PKG retail et **son fonctionnement sur PS5 13.60 reste non validé**.

**Moteur d'inspection :** l'application native embarque dorénavant le scanner de métadonnées dans son `eboot.bin`. `dist/native/cleanmyps5-helper.elf` est un payload **facultatif**, en lecture seule, sans navigateur, qui peut élargir la couverture selon ses propres permissions et sert des diagnostics via `127.0.0.1:8877`. Le frontend installé sonde directement quelques chemins de *son propre sandbox*, et en énumère au maximum 256 entrées immédiates par dossier accessible. Il tente également de lire l'espace disponible (`fstatvfs`) pour chaque montage réellement accessible, sans établir de total SSD. Il peut utiliser `/api/overview` du helper si celui-ci fonctionne déjà, mais ne dépend plus de son lancement : les sondes locales et le scan natif s'exécutent sur un worker pour éviter de figer VideoOut. Cercle demande une annulation coopérative ; la comparaison des doublons et le parcours des dossiers contrôlent cette demande, ne publient aucun résultat annulé comme complet et interdisent les workers concurrents. Un catalogue jusqu'à 10 000 médias découverts est consultable et filtrable à la manette, avec tris, pagination, recherche à la manette, regroupement par jeu, fiches de métadonnées et aperçus JPG/PNG locaux lorsque les fichiers sont accessibles (bornes strictes ; un JXR peut utiliser son JPG compagnon SDR sans décodage HDR ; aucun aperçu ni lecture vidéo). Un écran de diagnostic MP4/WebM consulte en plus durée et résolution par lecture bornée de métadonnées, sans extraction des images ni modification. ; les captures peuvent être marquées à revoir durant la session, sélection effacée au rescan. Toutes les limites du scan et des droits restent applicables ; elle n'autorise aucune suppression. Le frontend ne bénéficie pas automatiquement des privilèges du helper.

**Intégrité du livrable :** `dist/native/manifest.json` contient les empreintes SHA-256 de l'archive du titre, du helper distinct et, lorsqu'il est généré, du package natif UFS2 `PPSA99784.ffpkg`. `scripts/native_release_manifest.py check dist/native PPSA99784 <revision-40-hex>` permet de contrôler un dossier construit avec la révision indiquée. L'archive est inspectée pour refuser la traversée de chemins, les liens symboliques, les titres erronés et les entrées dupliquées. L'intégrité des fichiers ne démontre pas la compatibilité sur une console.

**Ancienne interface :** `make package` continue à fournir l'ancien `cleanmyps5.elf` avec interface WebKit, jusqu'à ce que les fonctions de consultation soient portées sur le frontend natif. C'est un produit de transition et non le format d'installation cible.

## Fonctions réellement disponibles

| Fonction | État vérifié |
|---|---|
| Interface native VideoOut, manette, 17 vues synthétiques | Code présent, navigation et mise en page testées sur hôte, compilation native CI réussie ; aucune capture d'exécution matérielle ni validation DualSense sur console |
| Sonde native de chemins applicatifs et des emplacements /data, /user | Ouverture **lecture seule**, indication accessible / inaccessible ou absent ; les codes d'erreur ne sont pas encore différenciés côté titre |
| Diagnostic détaillé de montages et permissions | Disponible dans le helper, sous réserve de sa visibilité filesystem |
| Inventaire captures, journaux, caches, homebrews, installations | Scanner borné en lecture seule **embarqué dans le titre natif** ; bilan agrégé, catalogue paginé de 10 000 médias maximum par scan, filtrable images/vidéos, jeu, âge, taille, avec tris taille/date/nom, couverture limitée aux droits du sandbox ; helper facultatif |
| Recherche, filtres, aperçus, doublons, export, Return to Live détaillé | Interface native : filtres, recherche via Options, aperçus JPG/PNG bornés, diagnostic MP4/WebM, doublons exacts bornés, bilan autoload et checklist en huit étapes. Export avancé et certaines fonctions de la version WebKit ne sont pas encore portés ; l'exécution réelle sur PS5 reste non vérifiée |
| Pré-analyse nettoyage native | Comptage des captures de 50 Mio et plus / anciennes de 90 jours par mtime, sans chemins transmis ni suppression permise |
| Homebrews et plugins natifs | Depuis Accès au stockage, Carré lance un scan borné en lecture seule et affiche les quatre compteurs protégés ; pas de désinstallation ni d'orphelin déclaré, résultats partiels signalés |
| Doublons exacts natifs | Analyse explicite via Triangle dans Nettoyage, images seulement, 128 fichiers/32 Mio, budget de 128 Mio de lectures ; statistiques agrégées et aucune écriture ni estimation de gain garanti |
| Effacement captures, caches, doublons ou résidus | **Désactivé**, aucun mécanisme d'effacement validé |
| Restauration, quarantaine, synchronisation base multimédia | Prototype SHA-256 de sauvegarde/restauration uniquement en tests hôte privés ; aucune restauration Galerie PS5 ni quarantaine native intégrée |
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
