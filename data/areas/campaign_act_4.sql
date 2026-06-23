-- data/areas/campaign_act_4.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('1_4_5_1', 'Act 4', 'Daresso''s Dream'),
    ('1_4_town', 'Act 4', 'Highgate'),
    ('1_4_4_1', 'Act 4', 'Kaom''s Dream'),
    ('1_4_4_3', 'Act 4', 'Kaom''s Stronghold'),
    ('1_4_1', 'Act 4', 'The Aqueduct'),
    ('1_4_7', 'Act 4', 'The Ascent'),
    ('1_4_6_1', 'Act 4', 'The Belly of the Beast Level 1'),
    ('1_4_6_2', 'Act 4', 'The Belly of the Beast Level 2'),
    ('1_4_3_3', 'Act 4', 'The Crystal Veins'),
    ('1_4_2', 'Act 4', 'The Dried Lake'),
    ('1_4_5_2', 'Act 4', 'The Grand Arena'),
    ('1_4_6_3', 'Act 4', 'The Harvest'),
    ('1_4_3_1', 'Act 4', 'The Mines Level 1'),
    ('1_4_3_2', 'Act 4', 'The Mines Level 2');

UPDATE areas SET subtype = 'Town' WHERE code = '1_4_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '1_4_7',    -- The Ascent
    '1_4_2',    -- The Dried Lake
    '1_4_3_1',  -- The Mines Level 1
    '1_4_3_2',  -- The Mines Level 2
    '1_4_5_1',  -- Daresso's Dream
    '1_4_5_2',  -- The Grand Arena
    '1_4_4_1',  -- Kaom's Dream
    '1_4_4_3',  -- Kaom's Stronghold
    '1_4_6_1',  -- The Belly of the Beast Level 1
    '1_4_6_2',  -- The Belly of the Beast Level 2
    '1_4_6_3'   -- The Harvest
);
