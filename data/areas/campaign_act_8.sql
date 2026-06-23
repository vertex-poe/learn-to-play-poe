-- data/areas/campaign_act_8.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('2_8_2_2', 'Act 8', 'Doedre''s Cesspool'),
    ('2_8_5', 'Act 8', 'The Bath House'),
    ('2_8_9', 'Act 8', 'The Grain Gate'),
    ('2_8_3', 'Act 8', 'The Grand Promenade'),
    ('2_8_13', 'Act 8', 'The Harbour Bridge'),
    ('2_8_4', 'Act 8', 'The High Gardens'),
    ('2_8_10', 'Act 8', 'The Imperial Fields'),
    ('2_8_6', 'Act 8', 'The Lunaris Concourse'),
    ('2_8_7_1_', 'Act 8', 'The Lunaris Temple Level 1'),
    ('2_8_7_2', 'Act 8', 'The Lunaris Temple Level 2'),
    ('2_8_8', 'Act 8', 'The Quay'),
    ('2_8_town', 'Act 8', 'The Sarn Encampment'),
    ('2_8_1', 'Act 8', 'The Sarn Ramparts'),
    ('2_8_11', 'Act 8', 'The Solaris Concourse'),
    ('2_8_12_1', 'Act 8', 'The Solaris Temple Level 1'),
    ('2_8_12_2', 'Act 8', 'The Solaris Temple Level 2'),
    ('2_8_2_1', 'Act 8', 'The Toxic Conduits');

UPDATE areas SET subtype = 'Town' WHERE code = '2_8_town';
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '2_8_8',   -- The Quay
    '2_8_3',   -- The Grand Promenade
    '2_8_4',   -- The High Gardens
    '2_8_13',  -- The Harbour Bridge
    '2_8_2_2', -- Doedre's Cesspool
    '2_8_7_2'  -- The Lunaris Temple Level 2
);
