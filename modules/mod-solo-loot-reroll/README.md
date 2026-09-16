# mod-solo-loot-reroll

Module AzerothCore / fork Playerbot destiné au jeu solo avec PlayerBots.

## Ce que fait exactement la version 1.0

Le module agit uniquement sur le **loot de créatures / monstres / boss**.

Il est actif dès qu'il existe **au moins un joueur réel éligible**.

- 1 humain seul : actif ;
- 1 humain + PlayerBots : actif ;
- plusieurs humains : actif ;
- plusieurs humains + PlayerBots : actif.

Quand plusieurs humains sont présents, le module choisit **au hasard un des joueurs réels éligibles pour chaque génération de loot**.

Le type d'armure, les armes utilisables et les notifications de substitution sont alors calculés pour **ce joueur tiré au sort**.

La détection des PlayerBots n'utilise ni le nom du personnage, ni `rndbot`, ni un préfixe de compte.
Elle utilise `WorldSession::IsBot()`, qui est une information native du fork AzerothCore Playerbot.

---

## Mode armure par défaut : AUTO

Aucune table SQL personnalisée n'est créée.

Chaque personnage est automatiquement considéré comme voulant porter **le type d'armure le plus élevé qu'il maîtrise actuellement** :

1. Plate / Plaque
2. Mail / Maille
3. Leather / Cuir
4. Cloth / Tissu

Le module utilise les compétences réelles du personnage (`SKILL_PLATE_MAIL`, `SKILL_MAIL`, `SKILL_LEATHER`).

C'est important en WotLK :

- un guerrier/paladin avant d'avoir appris la plaque vise naturellement la maille ;
- un chasseur/chaman avant d'avoir appris la maille vise naturellement le cuir ;
- une fois la nouvelle maîtrise apprise, AUTO bascule tout seul.

Il vérifie aussi `Player::CanUseItem()` pour ne pas proposer un objet que le personnage ne peut pas réellement utiliser.

### Commandes

```text
.loottypeset auto
.loottypeset off

.loottypeset tissu
.loottypeset cloth

.loottypeset cuir
.loottypeset leather

.loottypeset maille
.loottypeset mail

.loottypeset plaque
.loottypeset plate

.loottype
```

`auto` est le fonctionnement par défaut.

`off` désactive le smart-loot pour ce personnage **pour la session courante**.
Au prochain login/restart, il revient automatiquement en `auto`.

Les choix manuels sont volontairement non persistants afin de ne créer aucune table SQL supplémentaire.

---

## Quelles pièces d'armure sont concernées ?

Uniquement les vraies pièces d'armure :

- tête ;
- épaules ;
- torse / robe ;
- ceinture ;
- jambes ;
- pieds ;
- poignets ;
- mains.

Sous-classes :

- tissu ;
- cuir ;
- maille ;
- plaque.

Ne sont pas transformés comme "armure" :

- colliers ;
- anneaux ;
- bijoux ;
- capes ;
- chemises ;
- tabards ;
- sacs ;
- reliques.

---

## Armes

Les armes sont également prises en charge.

Pour une arme, le module ne tente pas de choisir un "build" ou une spécialisation.
Il utilise la règle robuste :

```text
Player::CanUseItem(item) == EQUIP_ERR_OK
```

Donc une arme déjà portable par le personnage est laissée tranquille.

Une arme qu'il ne peut pas utiliser peut être remplacée/compensée par une arme qu'il peut réellement équiper.

Les cannes à pêche sont explicitement exclues des candidats.

---

# Politique de rareté

La règle est maintenant : **même rareté d'abord**, puis **bonus de rareté inférieure uniquement si aucun remplacement de même rareté n'existe**.

## Drop vert inutile

Exemple :

```text
Drop normal : torse maille vert
Joueur : druide, AUTO = cuir
```

Si la table de loot du même monstre contient au moins un objet **vert** adapté :

```text
torse maille vert
        ↓
brassards cuir verts
```

Le slot n'a PAS besoin d'être identique.

Une tête peut devenir :

```text
brassards
gants
bottes
ceinture
...
```

tant que :

- c'est le même grand type de loot (armure -> armure, arme -> arme) ;
- c'est vert ;
- c'est utilisable ;
- pour une armure, c'est le type choisi/AUTO.

## Drop bleu / épique / supérieur inutile

La logique est maintenant la même pour toutes les raretés :

```text
1. Chercher un remplacement compatible de LA MÊME RARETÉ.
2. S'il existe : remplacer l'objet.
3. S'il n'existe pas : conserver l'objet original.
4. Chercher ensuite le meilleur objet compatible de rareté inférieure.
5. S'il existe : l'ajouter en BONUS.
```

Exemple épique :

```text
Drop original :
Épée épique inutilisable

Même pool contient :
Hache épique utilisable
```

Résultat :

```text
Hache épique utilisable
```

L'épée est remplacée.

Autre cas :

```text
Drop original :
Épée épique inutilisable

Aucun remplacement épique compatible.
Le pool contient :
Hache rare compatible
Hache verte compatible
```

Résultat :

```text
Épée épique inutilisable   <- conservée
Hache rare compatible      <- bonus
```

Le module choisit donc la **rareté inférieure la plus élevée disponible**.

Ordre de fallback :

```text
Légendaire -> Épique -> Rare -> Peu commun
Épique     -> Rare -> Peu commun
Rare       -> Peu commun
Peu commun -> pas de fallback inférieur
```

Même logique pour l'armure :

```text
Torse plaque épique
Joueur cuir

si brassards cuir épiques existent :
    torse plaque épique -> brassards cuir épiques

sinon, si bottes cuir rares existent :
    torse plaque épique conservé
    + bottes cuir rares en bonus

sinon, si seulement gants cuir verts existent :
    torse plaque épique conservé
    + gants cuir verts en bonus

sinon :
    aucune modification
```

Le slot n'a toujours pas besoin d'être identique.

### Options

```ini
SoloLootReroll.ReplaceRareOrHigher = 1
SoloLootReroll.LowerQualityFallbackBonus = 1
```

Avec `ReplaceRareOrHigher = 0`, les rares+ ne seront pas remplacés à qualité égale.
Avec `LowerQualityFallbackBonus = 0`, aucun bonus inférieur ne sera ajouté.

---

# "Même table de loot"

Le module ne choisit PAS un objet arbitraire dans `item_template`.

Pour un `creature_loot_template` donné, il construit le pool composé de :

- ses objets directs ;
- les objets accessibles via ses `reference_loot_template` ;
- y compris les références imbriquées.

Les entrées qui portent une condition de loot dans la table `conditions` sont **exclues comme candidats de remplacement**.

C'est volontaire : le module préfère ignorer un candidat plutôt que contourner accidentellement une condition AzerothCore.

Les entrées `QuestRequired` sont également exclues.

---

# Probabilités

Le module ne refait pas le roll complet du monstre.

AzerothCore génère d'abord son loot normalement.

Ensuite le hook :

```cpp
MiscScript::OnAfterLootTemplateProcess(...)
```

analyse le résultat.

Le choix d'un remplacement parmi plusieurs candidats est pondéré à partir des `Chance` de la table de loot.
Les entrées `Chance = 0` d'un groupe equal-chance reçoivent un poids égal.

Le système ne garantit pas de reproduire mathématiquement toutes les subtilités internes d'un réseau complexe de références/groupes, mais il conserve la pondération de base et n'autorise jamais une montée de qualité.

---

# Hooks AzerothCore utilisés

## Loot

```cpp
MiscScript::OnAfterLootTemplateProcess(...)
```

Ce hook est disponible dans la branche Playerbot récente.

Il est appelé par `Loot::FillLoot()` :

1. AzerothCore effectue les rolls normaux ;
2. les objets sont ajoutés au `Loot` ;
3. le module post-traite le `Loot` ;
4. AzerothCore calcule ensuite les règles d'accès/group loot.

Cela permet de remplacer proprement un `LootItem` sans modifier les lignes partagées du `LootTemplate` et sans patcher le core.

## Configuration

```cpp
WorldScript::OnBeforeConfigLoad(...)
```

## Nettoyage de préférence de session

```cpp
PlayerScript::OnPlayerLogout(...)
```

---

# Détection des PlayerBots

Le fork Playerbot ajoute à `WorldSession` :

```cpp
bool IsBot() const;
```

Le module utilise directement :

```cpp
player->GetSession()->IsBot()
```

C'est donc une information du fork lui-même.

Aucun de ces systèmes n'est utilisé :

```text
nom commençant par rndbot
nom de personnage
ID de compte prédéfini
liste SQL personnalisée
```

---

# Configuration

Fichier :

```text
conf/mod_solo_loot_reroll.conf.dist
```

Valeurs par défaut :

```ini
[worldserver]

SoloLootReroll.Enable = 1
SoloLootReroll.EnableArmor = 1
SoloLootReroll.EnableWeapons = 1
SoloLootReroll.MinQuality = 2

SoloLootReroll.ReplaceUncommon = 1
SoloLootReroll.ReplaceRareOrHigher = 1
SoloLootReroll.LowerQualityFallbackBonus = 1

SoloLootReroll.Debug = 0
```

---

# Installation AzerothCore classique

Placez le dossier ici :

```text
azerothcore-wotlk/
└── modules/
    └── mod-solo-loot-reroll/
```

Puis reconfigurez et recompilez AzerothCore de la même manière que votre build habituel.

Exemple Linux classique :

```bash
cd /chemin/vers/azerothcore-wotlk
mkdir -p build
cd build

cmake ../ -DCMAKE_INSTALL_PREFIX=/chemin/vers/azerothcore-server
cmake --build . --config Release -j$(nproc)
cmake --install . --config Release
```

Après installation, vérifiez la présence du modèle :

```text
etc/modules/mod_solo_loot_reroll.conf.dist
```

Créez le fichier actif :

```bash
cp etc/modules/mod_solo_loot_reroll.conf.dist \
   etc/modules/mod_solo_loot_reroll.conf
```

Modifiez ensuite le `.conf`.

---

# Installation dans votre Docker AzerothCore Playerbots

Votre dépôt :

```text
C:\azerothcore-playerbots
```

Le résultat doit être :

```text
C:\azerothcore-playerbots\modules\mod-solo-loot-reroll
```

## 1. Extraire le ZIP

Depuis PowerShell :

```powershell
cd C:\azerothcore-playerbots\modules
Expand-Archive -Path C:\CHEMIN\mod-solo-loot-reroll.zip -DestinationPath .
```

Vérifiez :

```powershell
Get-ChildItem C:\azerothcore-playerbots\modules\mod-solo-loot-reroll -Recurse
```

## 2. Recompiler le worldserver

```powershell
cd C:\azerothcore-playerbots

docker compose build --progress=plain ac-worldserver
```

Si Docker réutilise réellement une image qui ne prend pas le nouveau module en compte, seulement dans ce cas :

```powershell
docker compose build --no-cache --progress=plain ac-worldserver
```

Le `--no-cache` n'est pas nécessaire en temps normal.

## 3. Recréer uniquement le worldserver

```powershell
docker compose up -d --force-recreate ac-worldserver
```

Aucune opération sur les volumes SQL n'est nécessaire.

**NE PAS utiliser :**

```text
docker compose down -v
```

Le module ne crée aucune table et ne demande aucune réinitialisation de base.

Vos données restent dans les volumes existants :

- comptes ;
- personnages ;
- monde ;
- progression ;
- PlayerBots.

## 4. Vérifier les logs

```powershell
docker compose logs ac-worldserver |
Select-String -Pattern "solo-loot-reroll|SoloLootReroll"
```

Vous devez voir notamment :

```text
[mod-solo-loot-reroll] Enabled
[mod-solo-loot-reroll] Armor: true
[mod-solo-loot-reroll] Weapons: true
[mod-solo-loot-reroll] ReplaceUncommon: true
[mod-solo-loot-reroll] ReplaceRareOrHigher: false
[mod-solo-loot-reroll] LowerQualityFallbackBonus: true
[mod-solo-loot-reroll] Default player mode: Auto
```

Pour voir chaque remplacement :

```ini
SoloLootReroll.Debug = 1
```

---

# Configuration avec docker-compose.override.yml

Si votre image AzerothCore utilise la conversion standard des variables d'environnement de configuration,
vous pouvez également mettre sous `ac-worldserver.environment` :

```yaml
AC_SOLO_LOOT_REROLL_ENABLE: "1"
AC_SOLO_LOOT_REROLL_ENABLE_ARMOR: "1"
AC_SOLO_LOOT_REROLL_ENABLE_WEAPONS: "1"
AC_SOLO_LOOT_REROLL_MIN_QUALITY: "2"
AC_SOLO_LOOT_REROLL_REPLACE_UNCOMMON: "1"
AC_SOLO_LOOT_REROLL_REPLACE_RARE_OR_HIGHER: "1"
AC_SOLO_LOOT_REROLL_LOWER_QUALITY_FALLBACK_BONUS: "1"
AC_SOLO_LOOT_REROLL_DEBUG: "0"
```

Après modification d'une variable d'environnement :

```powershell
docker compose up -d --force-recreate ac-worldserver
```

Pas besoin de recompiler pour changer uniquement ces valeurs.

---

# Recompilation : quand est-elle nécessaire ?

## Recompilation nécessaire

Quand vous modifiez :

```text
src/mod_solo_loot_reroll.cpp
src/mod_solo_loot_reroll_loader.cpp
```

ou lorsque vous installez/mettez à jour le module.

## Pas de recompilation

Pour modifier :

```ini
SoloLootReroll.Enable
SoloLootReroll.EnableArmor
SoloLootReroll.EnableWeapons
SoloLootReroll.MinQuality
SoloLootReroll.ReplaceUncommon
SoloLootReroll.ReplaceRareOrHigher
SoloLootReroll.LowerQualityFallbackBonus
SoloLootReroll.Debug
```

Un rechargement de config peut suffire si le fichier actif est réellement modifié :

```text
.reload config
```

Dans Docker, si les valeurs viennent des variables d'environnement, il faut recréer le worldserver afin qu'il reçoive le nouvel environnement.

---

# Tests en jeu

## Test 1 - joueur seul

```text
.loottype
```

Doit indiquer :

```text
Group condition: active
1 real player
0 PlayerBot
```

Tuez plusieurs monstres possédant des armures vertes dans leur pool.

Un drop d'armure d'un type inférieur/inutilisable peut être transformé en armure verte du type AUTO.

## Test 2 - joueur + bots

Ajoutez des PlayerBots.

```text
.loottype
```

Le module reste actif :

```text
1 real player
4 PlayerBots
```

## Test 3 - deux humains

Connectez un second vrai joueur et groupez-le.

```text
.loottype
```

Doit afficher :

```text
Group condition: inactive
```

Aucun loot n'est modifié.

## Test 4 - type manuel bilingue

```text
.loottypeset cuir
.loottypeset leather
.loottypeset maille
.loottypeset mail
.loottypeset plaque
.loottypeset plate
.loottypeset tissu
.loottypeset cloth
```

## Test 5 - retour automatique

```text
.loottypeset auto
```

## Test 6 - rare incompatible

Avec :

```ini
SoloLootReroll.ReplaceRareOrHigher = 1
SoloLootReroll.LowerQualityFallbackBonus = 1
```

Un bleu incompatible est d'abord remplacé par un bleu compatible s'il en existe un.

Sinon le bleu original reste et un vert compatible peut être ajouté en bonus.

---

# Limitations assumées

1. Le module traite le loot de `creature_loot_template`, pas le désenchantement, le minage, la pêche, les coffres d'objets, etc.
2. Les entrées de loot ayant des conditions DB sont volontairement ignorées comme candidats de remplacement afin de ne pas contourner les conditions.
3. La pondération des références imbriquées est une approximation raisonnable de leurs chances relatives, pas une nouvelle exécution complète de l'algorithme de loot AzerothCore.
4. Le choix d'arme signifie **"le personnage peut l'utiliser"**, pas "best in slot", "bonne stat" ou "bonne spé".
5. Les choix `.loottypeset` manuels sont session-only ; AUTO est le défaut à chaque login.
6. Si une table de loot est rechargée manuellement en plein fonctionnement, utilisez ensuite `.reload config` ou redémarrez le worldserver afin de vider les caches du module.



---

# Notifications et annulation d'une substitution

Chaque modification de loot reçoit maintenant un ID unique créé par le module.

Exemple de remplacement :

```text
[SmartLoot #42] [Torse en plaques épique] -> [Brassards en cuir épiques]
(annuler: .subundo 42)
```

Les noms sont envoyés sous forme de **liens d'objets WoW cliquables**.

Pour annuler :

```text
.subundo 42
```

Le module vérifie que l'objet de substitution est réellement possédé par le personnage
(inventaire ou équipement, mais pas la banque).

## Cas 1 : véritable remplacement

Si SmartLoot a fait :

```text
Objet original A
    ->
Objet compatible B
```

alors :

```text
.subundo 42
```

fait :

```text
retire 1 x B
redonne 1 x A
```

Le module vérifie AVANT de retirer B qu'il y a suffisamment de place pour restaurer A.
Si ce n'est pas possible, il refuse l'opération et ne retire rien.

Le `randomPropertyId` original est également mémorisé et réutilisé lors de la restauration.

## Cas 2 : fallback en bonus

Si aucun objet de même rareté n'existait :

```text
Objet original A conserve
+ Objet inférieur B ajouté en bonus
```

alors l'annulation :

```text
.subundo 43
```

fait simplement :

```text
retire 1 x B
```

puisque A n'avait jamais été supprimé.

## Protections

Une annulation est refusée si :

- l'ID n'appartient pas à ce personnage ;
- l'ID n'existe plus dans l'historique ;
- l'annulation a déjà été faite ;
- l'objet de substitution n'est plus possédé ;
- l'inventaire n'a pas la place de restaurer l'objet original.

L'annulation n'est autorisée qu'une seule fois.

## Durée des IDs

Il n'y a toujours **aucune table SQL supplémentaire**.

Les IDs et leur historique sont conservés en RAM par le worldserver.

Par défaut :

```ini
SoloLootReroll.MaxUndoRecordsPerCharacter = 100
```

Les 100 dernières modifications de chaque personnage restent donc annulables
tant que le worldserver n'a pas redémarré.

Un relog du personnage ne supprime pas cet historique ; un redémarrage du worldserver, oui.

Configuration :

```ini
SoloLootReroll.NotifySubstitutions = 1
SoloLootReroll.MaxUndoRecordsPerCharacter = 100
```

Commande :

```text
.subundo <id>
```

Exemple :

```text
.subundo 42
```


---

# Groupes à plusieurs joueurs : sélection aléatoire

Le module n'est plus désactivé lorsqu'il y a plusieurs vrais joueurs.

Pour chaque loot de créature/boss :

```text
1. récupérer les vrais joueurs connectés du groupe ;
2. retirer ceux qui ont demandé à être ignorés ;
3. choisir uniformément un joueur parmi ceux qui restent ;
4. appliquer SmartLoot selon SON type d'armure / SES armes utilisables ;
5. envoyer le message SmartLoot et l'ID de substitution à ce joueur.
```

Exemple :

```text
Lucas    : cuir
Alice    : tissu
Marc     : plaque
+ 2 PlayerBots
```

Sur un boss, SmartLoot peut tirer :

```text
Alice
```

Le loot est donc adapté à Alice pour cette génération.

Au prochain monstre/boss, il peut tirer Lucas ou Marc.

Il n'y a pas de priorité permanente : tant qu'ils sont éligibles, chaque vrai joueur a la même chance d'être choisi.

---

# Ignorer SmartLoot pour favoriser les autres

Un joueur qui ne veut pas recevoir de substitutions peut faire :

```text
.lootignore on
```

Il est alors totalement retiré du tirage aléatoire.

Exemple :

```text
Lucas : eligible
Alice : .lootignore on
Marc  : eligible
```

Le module choisira uniquement entre :

```text
Lucas
Marc
```

Alice ne sera jamais prise comme cible SmartLoot tant qu'elle reste ignorée.

Pour revenir dans le tirage :

```text
.lootignore off
```

Pour voir l'état :

```text
.lootignore
```

ou :

```text
.lootignore status
```

La commande `.loottype` affiche également :

```text
nombre de vrais joueurs
nombre de joueurs eligibles
nombre de joueurs ignores
nombre de PlayerBots
votre propre etat ELIGIBLE / IGNORED
```

## Comportement solo

Si vous êtes seul :

```text
.lootignore off
```

-> SmartLoot fonctionne normalement.

Si vous êtes seul et faites :

```text
.lootignore on
```

-> aucun joueur éligible n'existe, donc SmartLoot ne modifie aucun loot.

## Persistance

Comme `.loottypeset`, l'état `.lootignore` est volontairement conservé uniquement pendant la session du personnage.

Au prochain login/restart :

```text
lootignore = off
```

Le joueur redevient éligible automatiquement.

Aucune table SQL supplémentaire n'est créée.
