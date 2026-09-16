-- mod-server-customization v1.7 damage debug command

DELETE FROM `command` WHERE `name` = 'damagedebug';

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('damagedebug', 0, 'Syntax: .damagedebug on|off|status - Trace incoming damage source/amount in chat and worldserver logs.');
