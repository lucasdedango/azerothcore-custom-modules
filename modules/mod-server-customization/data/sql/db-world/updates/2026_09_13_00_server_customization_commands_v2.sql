-- mod-server-customization v1.6 command cleanup
-- Keep only the current compact player command set.

DELETE FROM `command`
WHERE `name` IN (
    'bothelp','autohelp','bh',
    'mine','herb','gather','grind',
    'autostatus','as','autostop','astop',
    'grindhere','gh','grindzone','gz',
    'gatherzone','gg','minezone','mz','herbzone','hz'
);

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('bothelp',    0, 'Syntax: .bothelp - Show the automation command guide.'),
('autohelp',   0, 'Alias of .bothelp.'),
('bh',         0, 'Alias of .bothelp.'),
('mine',       0, 'Syntax: .mine - Route directly to active Mining nodes in the current zone.'),
('herb',       0, 'Syntax: .herb - Route directly to active Herbalism nodes in the current zone.'),
('gather',     0, 'Syntax: .gather - Route directly to active ore/herb nodes you can gather.'),
('grind',      0, 'Syntax: .grind - Grind suitable nearby mobs (old grindhere behaviour).'),
('autostatus', 0, 'Syntax: .autostatus - Show the current automation mode and gather target.'),
('as',         0, 'Alias of .autostatus.'),
('autostop',   0, 'Syntax: .autostop - Stop automation and fully detach selfbot AI.'),
('astop',      0, 'Alias of .autostop.');
