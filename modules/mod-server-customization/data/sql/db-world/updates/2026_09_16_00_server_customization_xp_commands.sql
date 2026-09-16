-- Player-facing XP commands are now owned by mod-server-customization.
DELETE FROM `command`
WHERE `name` IN ('xp', 'xp set', 'xp view', 'xp default', 'xp enable', 'xp disable');

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('xp',         0, 'Syntax: .xp $subcommand - Show the individual XP commands.'),
('xp set',     0, 'Syntax: .xp set X - Set your personal XP multiplier within the configured limits.'),
('xp view',    0, 'Syntax: .xp view - Show your personal XP multiplier.'),
('xp default', 0, 'Syntax: .xp default - Restore the configured default XP multiplier.'),
('xp enable',  0, 'Syntax: .xp enable - Resume XP gains for this character.'),
('xp disable', 0, 'Syntax: .xp disable - Stop XP gains for this character.');
