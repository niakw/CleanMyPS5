# Validation PS5 13.60 — premier lancement

**Statut : NON EXÉCUTÉ sur une PS5 réelle.** Une compilation croisée ne prouve pas la compatibilité avec WebKit et le firmware.

## Conditions et procédure (lecture seule)

1. Sur PS5 13.60, injecter une seule fois l'artefact `cleanmyps5.elf` depuis le chargeur habituel (Payload Manager / WebKit Autoloader). Ne pas activer le démarrage automatique pour cette première validation.
2. Relever la notification `CleanMyPS5: interface http://127.0.0.1:8877/` ; son absence ne prouve pas l'arrêt du payload.
3. Ouvrir `http://127.0.0.1:8877/` **sur la PS5 elle-même**. Le `127.0.0.1` du Mac n'est pas celui de la console ; aucune interface LAN n'est exposée.
4. Lancer « Analyser ma PS5 » et noter le nombre de fichiers et les avertissements. Ne pas interpréter cette taille comme tout l'espace du SSD.
5. Tester JPEG/PNG, MP4/WebM (selon les codecs supportés par le navigateur), puis les doublons octet à octet sans supprimer de fichiers.
6. Vérifier ✕, ○, L1/R1 et L2/R2 avec la DualSense, la fermeture des modales et la restauration du focus.
7. Consulter les montages et le diagnostic etaHEN, sans modifier les configurations ni les bases multimédias.
8. Arrêter le payload par le gestionnaire habituel et vérifier que le serveur ne répond plus.
9. Après extinction complète, vérifier qu'aucune entrée d'application ou de Paramètres n'a été installée. CleanMyPS5 ne crée aucun raccourci.

## Rapport utile

- Firmware, chargeur ELF, autoloader et SHA du commit testé.
- Démarrage/notification oui ou non ; WebKit loopback accessible oui ou non.
- Scan : nombre d'entrées, avertissements et durée observée.
- Manette, image, clip, doublons : résultats OK/KO.
- En cas d'échec, décrire l'étape sans exposer les sauvegardes, tokens ou fichiers de configuration personnels.

## Porte avant une version destructive

Vérifier les chemins réels, les APIs multimédias, la synchronisation avec les bases de captures et le cycle de vie du payload. Définir un export ou sauvegarde réversible, protéger les sauvegardes et titres, confirmer avant toute opération, puis vérifier la taille libérée. Aucune suppression générique de dossiers système ; aucune défragmentation SSD.

La version actuelle reste en lecture seule.

### Diagnostic USB

Le preflight examine à présent les répertoires autoloader candidats de `/mnt/usb0` à `/mnt/usb7` en lecture seule. La présence ou l'absence d'un montage n'établit pas à elle seule si le chargeur de payload est actif ; les vrais chemins doivent être confirmés sur la console 13.60.

## Variante expérimentale navigateur

Le projet produit désormais **deux ELF**. `cleanmyps5.elf` est inchangé (serveur loopback seul). `cleanmyps5-browser.elf` tente d'ouvrir `http://127.0.0.1:8877/` via les APIs de navigateur utilisées par l'exemple officiel du PS5 Payload SDK. Ne charger **qu'un seul** des deux. Tester la variante serveur seul avant de tenter la variante navigateur, car elle peut entrer en conflit avec un WebKit Autoloader déjà lancé. Consigner séparément les résultats d'initialisation de UserService, d'ouverture du navigateur et de navigation DualSense ; aucune installation persistante n'est effectuée.
