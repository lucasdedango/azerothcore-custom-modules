-- mod-server-customization automation wrapper commands
-- Normal player security level = 0
-- No quest automation commands are exposed.

DELETE FROM `command`
WHERE `name` IN (
    'bothelp','autohelp','bh',
    'grindhere','gh',
    'grindzone','gz',
    'gatherzone','gg',
    'minezone','mz',
    'herbzone','hz',
    'autostatus','as',
    'autostop','astop'
);

INSERT INTO `command` (`name`, `security`, `help`) VALUES
('bothelp',    0, 'Syntax: .bothelp - Show the restricted automation command guide.'),
('autohelp',   0, 'Alias of .bothelp.'),
('bh',         0, 'Alias of .bothelp.'),

('grindhere',  0, 'Syntax: .grindhere - Grind mobs around the current area.'),
('gh',         0, 'Alias of .grindhere.'),

('grindzone',  0, 'Syntax: .grindzone - Roam the zone and grind suitable mobs.'),
('gz',         0, 'Alias of .grindzone.'),

('gatherzone', 0, 'Syntax: .gatherzone - Roam and gather using known gathering professions.'),
('gg',         0, 'Alias of .gatherzone.'),

('minezone',   0, 'Syntax: .minezone - Roam with mining-focused loot filtering.'),
('mz',         0, 'Alias of .minezone.'),

('herbzone',   0, 'Syntax: .herbzone - Roam with herbalism-focused loot filtering.'),
('hz',         0, 'Alias of .herbzone.'),

('autostatus', 0, 'Syntax: .autostatus - Show whether restricted automation AI is active.'),
('as',         0, 'Alias of .autostatus.'),

('autostop',   0, 'Syntax: .autostop - Stop automation and fully detach selfbot AI.'),
('astop',      0, 'Alias of .autostop.');
