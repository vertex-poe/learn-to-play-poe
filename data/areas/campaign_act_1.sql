-- data/areas/campaign_act_1.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('1_1_town', 'Act 1', 'Lioneye''s Watch'),
    ('1_1_8', 'Act 1', 'Prisoner''s Gate'),
    ('1_1_11_2', 'Act 1', 'The Cavern of Anger'),
    ('1_1_11_1', 'Act 1', 'The Cavern of Wrath'),
    ('1_1_6', 'Act 1', 'The Climb'),
    ('1_1_2', 'Act 1', 'The Coast'),
    ('1_1_3a', 'Act 1', 'The Fetid Pool'),
    ('1_1_4_0', 'Act 1', 'The Flooded Depths'),
    ('1_1_5', 'Act 1', 'The Ledge'),
    ('1_1_7_1', 'Act 1', 'The Lower Prison'),
    ('1_1_3', 'Act 1', 'The Mud Flats'),
    ('1_1_9', 'Act 1', 'The Ship Graveyard'),
    ('1_1_9a', 'Act 1', 'The Ship Graveyard Cave'),
    ('1_1_4_1', 'Act 1', 'The Submerged Passage'),
    ('1_1_2a', 'Act 1', 'The Tidal Island'),
    ('1_1_1', 'Act 1', 'The Twilight Strand'),
    ('1_1_7_2', 'Act 1', 'The Upper Prison');

UPDATE areas SET subtype = 'Town' WHERE code = '1_1_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '1_1_1',    -- The Twilight Strand
    '1_1_2a',   -- The Tidal Island
    '1_1_3',    -- The Mud Flats
    '1_1_3a',   -- The Fetid Pool
    '1_1_4_0',  -- The Flooded Depths
    '1_1_9',    -- The Ship Graveyard
    '1_1_9a',   -- The Ship Graveyard Cave
    '1_1_7_2',  -- The Upper Prison
    '1_1_11_2'  -- The Cavern of Anger
);
