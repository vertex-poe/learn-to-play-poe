-- data/areas/campaign_act_9.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('2_9_town', 'Act 9', 'Highgate'),
    ('2_9_10_1', 'Act 9', 'The Belly of the Beast'),
    ('2_9_1', 'Act 9', 'The Blood Aqueduct'),
    ('2_9_6', 'Act 9', 'The Boiling Lake'),
    ('2_9_2', 'Act 9', 'The Descent'),
    ('2_9_5', 'Act 9', 'The Foothills'),
    ('2_9_4', 'Act 9', 'The Oasis'),
    ('2_9_8', 'Act 9', 'The Quarry'),
    ('2_9_9', 'Act 9', 'The Refinery'),
    ('2_9_10_2', 'Act 9', 'The Rotting Core'),
    ('2_9_7', 'Act 9', 'The Tunnel'),
    ('2_9_3', 'Act 9', 'The Vastiri Desert');

UPDATE areas SET subtype = 'Town' WHERE code = '2_9_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '2_9_2',    -- The Descent
    '2_9_4',    -- The Oasis
    '2_9_6',    -- The Boiling Lake
    '2_9_9',    -- The Refinery
    '2_9_10_1', -- The Belly of the Beast
    '2_9_10_2'  -- The Rotting Core
);
