-- POE1 base classes. Ascendancy classes are not tracked here; they surface
-- as character names that point back to one of these seven bases.
INSERT OR IGNORE INTO classes (name) VALUES
    ('Duelist'),
    ('Marauder'),
    ('Ranger'),
    ('Scion'),
    ('Shadow'),
    ('Templar'),
    ('Witch');

-- Future seed tables go here (passive_quest_sources, etc.)
