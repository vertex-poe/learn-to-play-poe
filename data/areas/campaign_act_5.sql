-- data/areas/campaign_act_5.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('1_5_3', 'Act 5', 'Oriath Square'),
    ('1_5_town', 'Act 5', 'Overseer''s Tower'),
    ('1_5_8', 'Act 5', 'The Cathedral Rooftop'),
    ('1_5_5', 'Act 5', 'The Chamber of Innocence'),
    ('1_5_2', 'Act 5', 'The Control Blocks'),
    ('1_5_6', 'Act 5', 'The Ossuary'),
    ('1_5_7', 'Act 5', 'The Reliquary'),
    ('1_5_3b', 'Act 5', 'The Ruined Square'),
    ('1_5_1', 'Act 5', 'The Slave Pens'),
    ('1_5_4', 'Act 5', 'The Templar Courts'),
    ('1_5_4b', 'Act 5', 'The Torched Courts');

UPDATE areas SET subtype = 'Town' WHERE code = '1_5_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '1_5_2',   -- The Control Blocks
    '1_5_6',   -- The Ossuary
    '1_5_4b'   -- The Torched Courts
);
