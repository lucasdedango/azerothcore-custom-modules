-- mod-mount-chest V2
-- Safe V1 -> V2 upgrade script.
-- Removes the old V1 chest/loot definitions and recreates the three V2 chest templates.
-- V2 rewards are granted by C++ on right-click, so gameobject_loot_template is intentionally unused.

SET @COPPER_CHEST_ENTRY := 900001;
SET @SILVER_CHEST_ENTRY := 900002;
SET @MOUNT_CHEST_ENTRY  := 900003;

-- Remove V1 and any previous V2 loot/template data first.
DELETE FROM `gameobject_loot_template`
WHERE `Entry` IN (@COPPER_CHEST_ENTRY, @SILVER_CHEST_ENTRY, @MOUNT_CHEST_ENTRY);

DELETE FROM `gameobject_template_addon`
WHERE `entry` IN (@COPPER_CHEST_ENTRY, @SILVER_CHEST_ENTRY, @MOUNT_CHEST_ENTRY);

DELETE FROM `gameobject_template`
WHERE `entry` IN (@COPPER_CHEST_ENTRY, @SILVER_CHEST_ENTRY, @MOUNT_CHEST_ENTRY);

-- Reuse three valid chest display IDs already present in the world DB.
SET @DISPLAY_1 := (
    SELECT `displayId`
    FROM `gameobject_template`
    WHERE `type` = 3 AND `displayId` <> 0
    GROUP BY `displayId`
    ORDER BY `displayId`
    LIMIT 1 OFFSET 0
);

SET @DISPLAY_2 := (
    SELECT `displayId`
    FROM `gameobject_template`
    WHERE `type` = 3 AND `displayId` <> 0
    GROUP BY `displayId`
    ORDER BY `displayId`
    LIMIT 1 OFFSET 1
);

SET @DISPLAY_3 := (
    SELECT `displayId`
    FROM `gameobject_template`
    WHERE `type` = 3 AND `displayId` <> 0
    GROUP BY `displayId`
    ORDER BY `displayId`
    LIMIT 1 OFFSET 2
);

SET @DISPLAY_2 := COALESCE(@DISPLAY_2, @DISPLAY_1);
SET @DISPLAY_3 := COALESCE(@DISPLAY_3, @DISPLAY_2, @DISPLAY_1);

INSERT INTO `gameobject_template`
(
    `entry`, `type`, `displayId`, `name`, `IconName`, `castBarCaption`, `unk1`, `size`,
    `Data0`, `Data1`, `Data2`, `Data3`, `Data4`, `Data5`, `Data6`, `Data7`,
    `Data8`, `Data9`, `Data10`, `Data11`, `Data12`, `Data13`, `Data14`, `Data15`,
    `Data16`, `Data17`, `Data18`, `Data19`, `Data20`, `Data21`, `Data22`, `Data23`,
    `AIName`, `ScriptName`, `VerifiedBuild`
)
VALUES
(
    @COPPER_CHEST_ENTRY, 3, @DISPLAY_1, 'Lucky Copper Chest', '', '', '', 1,
    0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    '', 'go_mount_chest_v2', 0
),
(
    @SILVER_CHEST_ENTRY, 3, @DISPLAY_2, 'Lucky Silver Chest', '', '', '', 1,
    0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    '', 'go_mount_chest_v2', 0
),
(
    @MOUNT_CHEST_ENTRY, 3, @DISPLAY_3, 'Lucky Mount Chest', '', '', '', 1,
    0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    '', 'go_mount_chest_v2', 0
);
