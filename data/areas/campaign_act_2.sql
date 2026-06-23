-- data/areas/campaign_act_2.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('1_2_14_3', 'Act 2', 'The Ancient Pyramid'),
    ('1_2_4', 'Act 2', 'The Broken Bridge'),
    ('1_2_14_2', 'Act 2', 'The Caverns'),
    ('1_2_6_1', 'Act 2', 'The Chamber of Sins Level 1'),
    ('1_2_6_2', 'Act 2', 'The Chamber of Sins Level 2'),
    ('1_2_3', 'Act 2', 'The Crossroads'),
    ('1_2_5_1', 'Act 2', 'The Crypt Level 1'),
    ('1_2_5_2', 'Act 2', 'The Crypt Level 2'),
    ('1_2_2a', 'Act 2', 'The Den'),
    ('1_2_13', 'Act 2', 'The Dread Thicket'),
    ('1_2_15', 'Act 2', 'The Fellshrine Ruins'),
    ('1_2_town', 'Act 2', 'The Forest Encampment'),
    ('1_2_8', 'Act 2', 'The Northern Forest'),
    ('1_2_2', 'Act 2', 'The Old Fields'),
    ('1_2_7', 'Act 2', 'The Riverways'),
    ('1_2_1', 'Act 2', 'The Southern Forest'),
    ('1_2_11', 'Act 2', 'The Vaal Ruins'),
    ('1_2_10', 'Act 2', 'The Weaver''s Chambers'),
    ('1_2_9', 'Act 2', 'The Western Forest'),
    ('1_2_12', 'Act 2', 'The Wetlands');

UPDATE areas SET subtype = 'Town' WHERE code = '1_2_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '1_2_2',    -- The Old Fields
    '1_2_2a',   -- The Den
    '1_2_15',   -- The Fellshrine Ruins
    '1_2_10',   -- The Weaver's Chambers
    '1_2_11',   -- The Vaal Ruins
    '1_2_13',   -- The Dread Thicket
    '1_2_5_2',  -- The Crypt Level 2
    '1_2_6_2'   -- The Chamber of Sins Level 2
);
