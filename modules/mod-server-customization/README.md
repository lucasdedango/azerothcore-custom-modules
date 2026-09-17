# mod-server-customization v2.4.1

Module maison pour les personnalisations globales du serveur et l'automatisation joueur contrôlée.

## Commandes actuelles

- `.mine` : cherche les **vrais spawns actifs** de minerai dans la zone actuelle, vérifie le niveau de Mining et un chemin navmesh valide, puis pathfind directement vers le filon.
- `.herb` : même logique pour Herbalism.
- `.gather` : cible minerai + plantes que le personnage sait réellement récolter.
- `.grind` : grind natif des mobs adaptés autour du personnage, équivalent à l'ancien `.grindhere` ; pas de New RPG roaming.
- `.damagedebug on|off|status` : trace les dégâts entrants (montant, source, niveau/entry et, pour les dégâts magiques/périodiques, le sort quand disponible) dans le chat et les logs du worldserver.
- `.autostatus` / `.as` : affiche le mode et, pour la récolte, le spawn ciblé.
- `.autostop` / `.astop` : arrête l'automatisation et détache complètement le selfbot AI.
- `.bothelp` / `.autohelp` / `.bh` : aide en jeu.

Les anciennes commandes `.grindhere`, `.gh`, `.grindzone`, `.gz`, `.gatherzone`, `.gg`, `.minezone`, `.mz`, `.herbzone`, `.hz` sont supprimées.

## Récolte dirigée : ce qui change

Les versions précédentes utilisaient `new rpg` + `wander random`. Le bot pouvait donc tourner en rond jusqu'à tomber par hasard sur un node.

La v1.8 ne fait plus ça. Pour `.mine`, `.herb` et `.gather`, le module :

1. parcourt `ObjectMgr::GetAllGOData()` pour récupérer les positions DB réelles des GameObjects ;
2. garde uniquement la map et, par défaut, la zone actuelle ;
3. identifie les nodes Mining/Herbalism via le Lock.dbc du GameObject ;
4. vérifie que le joueur possède le métier et le niveau de compétence nécessaire ;
5. ignore les spawns actuellement indisponibles : pool inactif, spawn group inactif, respawn en attente, mauvaise phase ;
6. trie d'abord les candidats par distance à vol d'oiseau et garde les `PathCandidates` plus proches ;
7. génère 8 points d'approche autour de chaque node, à `ApproachDistance` yards ;
8. recale le Z de chaque point sur le terrain puis demande à `PathGenerator` un chemin `PATHFIND_NORMAL` ;
9. compare la **longueur réelle des chemins navmesh** et choisit le couple node/point d'approche ayant le chemin le plus court ;
10. déplace le personnage vers ce point d'approche plutôt que vers le centre exact du minerai/plante ;
11. à proximité, laisse Playerbots `gather` + `loot` effectuer l'interaction finale ;
12. après disparition du node, choisit automatiquement le suivant.

Les cibles impossibles / bloquées sont temporairement blacklistées afin d'éviter de tourner autour du même filon.

## Combat pendant la récolte

Le patch Playerbots conserve la priorité aux véritables attaquants. En mode `.mine`, `.herb` ou `.gather`, il n'autorise ensuite aucun ciblage volontaire de mob pour grind.

Donc :

- un mob non aggro est ignoré ;
- un mob qui aggro le personnage est combattu normalement ;
- après le combat, le contrôleur reprend la route vers le node.

`.grind` n'utilise pas cette restriction : c'est un mode grind normal.

## Configuration du routing

Dans `conf/mod_server_customization.conf.dist` :

```ini
ServerCustomization.GatherRoute.Enable = 1
ServerCustomization.GatherRoute.SameZoneOnly = 1
ServerCustomization.GatherRoute.SearchRadius = 250
ServerCustomization.GatherRoute.UpdateIntervalMs = 1500
ServerCustomization.GatherRoute.PathCandidates = 12
ServerCustomization.GatherRoute.ApproachDistance = 6
ServerCustomization.GatherRoute.TargetTimeoutMs = 120000
ServerCustomization.GatherRoute.BlacklistMs = 45000
```

## Starter package

À la première connexion d'un personnage qui ne connaît pas encore Apprentice Riding (33388) :

- Apprentice Riding + Riding 75/75 ;
- monture raciale 60 % ;
- +1 pièce d'argent ;
- monture placée sur une case libre de la première barre.

## Installation / mise à jour

Remplace le dossier :

```text
C:\azerothcore-playerbots\modules\mod-server-customization
```

Puis :

```powershell
cd C:\azerothcore-playerbots
powershell -ExecutionPolicy Bypass -File .\modules\mod-server-customization\apply-playerbots-selfbot-lock.ps1
powershell -ExecutionPolicy Bypass -File .\modules\mod-server-customization\apply-playerbots-gather-only.ps1
powershell -ExecutionPolicy Bypass -File .\rebuild-azerothcore.ps1
```

Le SQL `2026_09_13_00_server_customization_commands_v2.sql` nettoie automatiquement les anciennes commandes via l'updater AzerothCore.

Après une mise à jour de `mod-playerbots`, réapplique les deux scripts avant le rebuild.

## Selfbot restreint

Réglage Playerbots recommandé :

```ini
AiPlayerbot.SelfBotLevel = 1
```

Les joueurs normaux ne peuvent pas exploiter le selfbot natif directement ; seules les commandes explicitement exposées ici l'attachent.


## Sélection de route v1.8

`SearchRadius` est l'unique limite de recherche et est recalculé autour de la position **actuelle** du personnage :

```ini
ServerCustomization.GatherRoute.SearchRadius = 250
```

Il n'y a plus de `MaxSegmentDistance`. Après chaque récolte, le prochain scan repart depuis la nouvelle position du personnage.

Pour éviter les trajectoires qui collent les falaises, le module ne vise plus le centre exact d'un node. Pour chaque ressource candidate il génère 8 points régulièrement espacés sur un cercle autour du spawn, recale leur hauteur sur le terrain, puis laisse AzerothCore construire le chemin navmesh vers chacun. Parmi les `PathCandidates` nodes les plus proches à vol d'oiseau, le module retient finalement celui dont le meilleur point d'approche produit le **chemin navmesh réel le plus court**.

Les points intermédiaires du trajet ne sont pas inventés par le module : ils sont générés par le `PathGenerator` d'AzerothCore à partir du navmesh. Le module choisit seulement la bonne destination d'approche.

## Diagnostic dégâts

```text
.damagedebug on
.damagedebug status
.damagedebug off
```

Chaque dégât entrant affiche la source, le montant, les PV avant impact et `LETHAL` si le coup suffit à tuer le personnage. Les hooks de dégâts de sort / périodiques ajoutent aussi le nom et l'ID du sort quand AzerothCore les fournit. Les mêmes informations sont écrites dans les logs du worldserver.


## v1.9 — mount-aware gather + immediate aggro handoff

- `.mine`, `.herb`, `.gather` explicitly enable Playerbots' native `mount` strategy.
- Long gather legs briefly wait for a known mount to be cast, then route anyway if mounting fails.
- The character dismounts near the resource for native gather interaction.
- Attacker/combat detection runs before the gather route timer; directed movement is cancelled immediately once, then native Playerbots combat movement takes over.
- The current resource target is preserved through combat and resumed afterwards if still valid.
- Starter repair no longer returns early just because Apprentice Riding is already known: an old/player-created character missing any mount can receive the configured racial starter mount without receiving starter money again.


## v2.0 — mounted aggro passthrough + grindtarget

- Directed gather can stay mounted and continue the route through simple aggro while no damage is received.
- Any incoming damage starts a configurable combat-priority hold (default 3000 ms).
- Hard control or forced dismount still yields immediately to combat.
- Playerbots' grind attacker selection and mount-state action are patched so this mounted pass-through is respected.
- `.grind` remains the normal nearby-mob grind mode.
- `.grindtarget` reads the currently selected creature entry and restricts voluntary grind targets to that creature type.
- Attackers are always allowed to override `.grindtarget`, so the bot still defends itself.


## v2.1 — hostile route avoidance

- Directed gather now scores each real navmesh path against nearby hostile creatures.
- A hostile creature contributes a penalty when the path enters its calculated aggro radius plus a configurable safety buffer.
- Higher-level enemies are penalized more; elites/world bosses receive an additional multiplier.
- The selected gather node/approach point is now the lowest total cost: real path length + hostile-route risk.
- `.grindtarget` keeps ignoring unrelated creature entries as grind targets, and now prefers an instance of the requested mob whose navmesh path crosses fewer unrelated aggro zones.
- The intended `.grindtarget` creature entry is excluded from the avoidance penalty.
- This is risk-aware target/path selection; it does not alter AzerothCore's navmesh polygons.


## v2.2 — mount / interaction / loot / recovery / fall safety

- Directed gather can use a genuinely known mount even when Playerbots' global `useGroundMountAtMinLevel` is higher than the character level.
- Default resource approach reduced from 6 yd to 3.5 yd; final hand-off threshold reduced to +0.75 yd.
- Default gather dismount distance reduced from 10 yd to 7 yd.
- Gather routing freezes while `Player::GetLootGUID()` is active, so the route cannot re-open/re-path the node while its loot window is still being processed.
- Playerbots `StoreLootAction` is patched so directed gather does not discard mining/herb loot because of normal item-usage filters.
- New HP/mana recovery hysteresis: stop below Start thresholds, resume only when BOTH applicable Resume thresholds are reached. Attackers still override recovery for self-defense.
- Recovery state is shared with `GrindTargetValue`, preventing a new voluntary grind target while resting.
- Fall workaround: while custom automation is active, source-less damage during or up to 5 s after a detected falling state is suppressed. This targets delayed movement-generated fall damage without globally disabling environmental damage.


## v2.3 — cliff-aware approaches + optional no-mob-loot

### Cliff / ledge resources

The route planner no longer accepts a point just because its XY is close to a node.

For each candidate resource it now:

1. tests several concentric rings around the node;
2. tests multiple angles on each ring;
3. snaps every candidate to the local terrain Z;
4. rejects the candidate if its **final 3-D distance** from the resource is outside the configured interaction distance;
5. requires a normal navmesh path from the current player position;
6. scores all surviving paths for length + hostile-route risk.

This prevents a lower cliff shelf from being selected when the actual mineable spot is on the upper ledge.

A near-target stuck detector additionally blacklists the resource for 90 seconds after repeated updates with insufficient progress. Near the final approach the native Playerbots mount strategy is suppressed, preventing mount/dismount loops.

### No creature loot while gathering

The gather commands now accept an optional `noloot` argument:

```text
.mine noloot
.herb noloot
.gather noloot
```

Combat/self-defense is unchanged. When a creature dies on the way, its corpse is removed from Playerbots' available-loot queue. Mining/herbalism GameObject loot remains enabled.

Running the commands without `noloot` preserves normal loot behavior.

## v2.4 — XP individuelle et gains de métiers

`mod-server-customization` fournit désormais directement les commandes `.xp` qui étaient auparavant
assurées par `mod-individual-xp` :

```text
.xp view
.xp set 1
.xp set 3
.xp default
.xp enable
.xp disable
```

Le multiplicateur est sauvegardé par personnage dans la table `server_customization_character_rates`. La migration y copie d’abord les anciennes valeurs `individualxp`. Cette table historique est conservée comme sauvegarde, mais le module ne la lit plus : les réglages des personnages sont préservés sans dépendance d’exécution à l’ancien module. Les bornes
et la valeur par défaut sont configurables :

```ini
ServerCustomization.XP.Enable = 1
ServerCustomization.XP.AnnounceOnLogin = 1
ServerCustomization.XP.DefaultRate = 1
ServerCustomization.XP.MaxRate = 3
```

Les points gagnés lors d'un skill-up de récolte de plante/minerai ou d'un craft sont également
gérés par personnage. Les chances orange/jaune/verte/grise natives restent inchangées ; ces
commandes choisissent entre 1 et 3 points accordés lorsque le skill-up réussit :

```text
.profession view
.profession gathering 1
.profession crafting 3
.profession default
```

Les valeurs initiales et celles restaurées par `.profession default` sont configurables :

```ini
ServerCustomization.Profession.Enable = 1
ServerCustomization.Profession.DefaultGatheringSkillGain = 1
ServerCustomization.Profession.DefaultCraftingSkillGain = 3
```

## v2.5 — démarrage fiable de `.grindtarget`

- `.grindtarget` refuse désormais une sélection qui n'est pas une créature hostile vivante.
- Après avoir mémorisé l'entry, la commande désélectionne le mob utilisé pour la configuration
  et réinitialise les cibles `current target` et `pull target` de Playerbots.
- Le trigger natif `no target` peut ainsi lancer `attack anything` au tick suivant ; le filtre
  custom continue ensuite de limiter les cibles volontaires à l'entry mémorisée.

## v2.6 — `.grind` et `.grindtarget` acceptent les créatures grises

- `.grind` peut maintenant sélectionner les créatures hostiles adaptées à portée même si leur
  niveau est assez bas pour ne plus donner d'expérience.
- Une créature hostile explicitement choisie avec `.grindtarget` reste elle aussi une cible
  valide lorsqu'elle est grise.
- Le grind Playerbots natif, utilisé hors de ces commandes custom, conserve son filtre XP/honneur.
- Les véritables attackers gardent la priorité, comme auparavant.
