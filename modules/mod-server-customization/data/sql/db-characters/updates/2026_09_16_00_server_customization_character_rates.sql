-- Move existing per-character XP settings into the table owned by
-- mod-server-customization and add personal profession skill gains.
CREATE TABLE IF NOT EXISTS `individualxp` (
  `CharacterGUID` INT UNSIGNED NOT NULL,
  `XPRate` FLOAT NOT NULL DEFAULT 1,
  PRIMARY KEY (`CharacterGUID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

CREATE TABLE IF NOT EXISTS `server_customization_character_rates` (
  `CharacterGUID` INT UNSIGNED NOT NULL,
  `XPRate` FLOAT NOT NULL DEFAULT 1,
  `GatheringSkillGain` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `CraftingSkillGain` TINYINT UNSIGNED NOT NULL DEFAULT 3,
  PRIMARY KEY (`CharacterGUID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8;

INSERT IGNORE INTO `server_customization_character_rates`
  (`CharacterGUID`, `XPRate`, `GatheringSkillGain`, `CraftingSkillGain`)
SELECT `CharacterGUID`, `XPRate`, 1, 3
FROM `individualxp`;

-- The legacy table is deliberately kept as a backup; runtime code no longer reads it.
