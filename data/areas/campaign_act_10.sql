-- data/areas/campaign_act_10.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('2_10_town', 'Act 10', 'Oriath Docks'),
    ('2_10_5', 'Act 10', 'The Canals'),
    ('2_10_1', 'Act 10', 'The Cathedral Rooftop'),
    ('2_10_7', 'Act 10', 'The Control Blocks'),
    ('2_10_4', 'Act 10', 'The Desecrated Chambers'),
    ('2_10_6', 'Act 10', 'The Feeding Trough'),
    ('2_10_9', 'Act 10', 'The Ossuary'),
    ('2_10_2', 'Act 10', 'The Ravaged Square'),
    ('2_10_8', 'Act 10', 'The Reliquary'),
    ('2_10_3', 'Act 10', 'The Torched Courts');

UPDATE areas SET subtype = 'Town' WHERE code = '2_10_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '2_10_1',  -- The Cathedral Rooftop
    '2_10_9',  -- The Ossuary
    '2_10_5',  -- The Canals
    '2_10_6',  -- The Feeding Trough
    '2_10_3'   -- The Torched Courts
);
