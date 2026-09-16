-- Player-facing personal profession gain commands.
DELETE FROM `command`
WHERE `name` IN ('profession', 'profession gathering', 'profession crafting', 'profession view', 'profession default');

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('profession',           0, 'Syntax: .profession $subcommand - Show the personal profession gain commands.'),
('profession gathering', 0, 'Syntax: .profession gathering X - Set Mining/Herbalism skill points gained (1 to 3).'),
('profession crafting',  0, 'Syntax: .profession crafting X - Set crafting skill points gained (1 to 3).'),
('profession view',      0, 'Syntax: .profession view - Show your personal profession skill gains.'),
('profession default',   0, 'Syntax: .profession default - Restore the configured profession defaults.');
