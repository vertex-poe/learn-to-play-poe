-- data/areas/campaign_act_6.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('2_6_town', 'Act 6', 'Lioneye''s Watch'),
    ('2_6_8', 'Act 6', 'Prisoner''s Gate'),
    ('2_6_7_2', 'Act 6', 'Shavronne''s Tower'),
    ('2_6_14', 'Act 6', 'The Beacon'),
    ('2_6_15', 'Act 6', 'The Brine King''s Reef'),
    ('2_6_13', 'Act 6', 'The Cavern of Anger'),
    ('2_6_2', 'Act 6', 'The Coast'),
    ('2_6_5', 'Act 6', 'The Karui Fortress'),
    ('2_6_7_1', 'Act 6', 'The Lower Prison'),
    ('2_6_4', 'Act 6', 'The Mud Flats'),
    ('2_6_6', 'Act 6', 'The Ridge'),
    ('2_6_10', 'Act 6', 'The Riverways'),
    ('2_6_12', 'Act 6', 'The Southern Forest'),
    ('2_6_3', 'Act 6', 'The Tidal Island'),
    ('2_6_1', 'Act 6', 'The Twilight Strand'),
    ('2_6_9', 'Act 6', 'The Western Forest'),
    ('2_6_11', 'Act 6', 'The Wetlands');

UPDATE areas SET subtype = 'Town' WHERE code = '2_6_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '2_6_1',   -- The Twilight Strand
    '2_6_3',   -- The Tidal Island
    '2_6_4',   -- The Mud Flats
    '2_6_5',   -- The Karui Fortress
    '2_6_11',  -- The Wetlands
    '2_6_13',  -- The Cavern of Anger
    '2_6_7_2'  -- Shavronne's Tower
);
