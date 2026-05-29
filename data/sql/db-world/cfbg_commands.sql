DELETE FROM `command` WHERE `name` IN ('cfbg', 'cfbg race', 'cfbg debug');
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('cfbg', 0, 'Crossfaction battleground module commands.'),
('cfbg race', 0, 'Morphs your character to the selected race once you join a battleground.\nBy default, the following races are available: human, dwarf, gnome, draenei ("broken ones"), orc, bloodelf, troll, tauren.'),
('cfbg debug', 1, 'Syntax: .cfbg debug [$playername]\nDumps the target/self player''s CFBG state: enable flags, fake/native/forget flags, native vs current race/team/display, preferred race setting, and the stored FakePlayer record when faked. Console requires $playername.');
