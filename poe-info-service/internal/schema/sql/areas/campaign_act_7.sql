-- data/areas/campaign_act_7.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('2_7_5_map', 'Act 7', 'Maligaro''s Sanctum'),
    ('2_7_7', 'Act 7', 'The Ashen Fields'),
    ('2_7_town', 'Act 7', 'The Bridge Encampment'),
    ('2_7_1', 'Act 7', 'The Broken Bridge'),
    ('2_7_10', 'Act 7', 'The Causeway'),
    ('2_7_5_1', 'Act 7', 'The Chamber of Sins Level 1'),
    ('2_7_5_2', 'Act 7', 'The Chamber of Sins Level 2'),
    ('2_7_2', 'Act 7', 'The Crossroads'),
    ('2_7_4', 'Act 7', 'The Crypt'),
    ('2_7_6', 'Act 7', 'The Den'),
    ('2_7_9', 'Act 7', 'The Dread Thicket'),
    ('2_7_3', 'Act 7', 'The Fellshrine Ruins'),
    ('2_7_8', 'Act 7', 'The Northern Forest'),
    ('2_7_12_1', 'Act 7', 'The Temple of Decay Level 1'),
    ('2_7_12_2', 'Act 7', 'The Temple of Decay Level 2'),
    ('2_7_11', 'Act 7', 'The Vaal City');

UPDATE areas SET subtype = 'Town' WHERE code = '2_7_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '2_7_1',    -- The Broken Bridge
    '2_7_3',    -- The Fellshrine Ruins
    '2_7_5_map', -- Maligaro's Sanctum
    '2_7_9',    -- The Dread Thicket
    '2_7_12_1', -- The Temple of Decay Level 1
    '2_7_12_2', -- The Temple of Decay Level 2
    '2_7_5_2'   -- The Chamber of Sins Level 2
);
