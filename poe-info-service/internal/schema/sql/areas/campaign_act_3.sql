-- data/areas/campaign_act_3.sql (sql)

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('1_3_17_2', 'Act 3', 'The Archives'),
    ('1_3_7', 'Act 3', 'The Battlefront'),
    ('1_3_6', 'Act 3', 'The Catacombs'),
    ('1_3_1', 'Act 3', 'The City of Sarn'),
    ('1_3_3_1', 'Act 3', 'The Crematorium'),
    ('1_3_9', 'Act 3', 'The Docks'),
    ('1_3_13', 'Act 3', 'The Ebony Barracks'),
    ('1_3_15', 'Act 3', 'The Imperial Gardens'),
    ('1_3_17_1', 'Act 3', 'The Library'),
    ('1_3_14_1', 'Act 3', 'The Lunaris Temple Level 1'),
    ('1_3_14_2', 'Act 3', 'The Lunaris Temple Level 2'),
    ('1_3_5', 'Act 3', 'The Marketplace'),
    ('1_3_town', 'Act 3', 'The Sarn Encampment'),
    ('1_3_18_1', 'Act 3', 'The Sceptre of God'),
    ('1_3_10_1', 'Act 3', 'The Sewers'),
    ('1_3_2', 'Act 3', 'The Slums'),
    ('1_3_8_1', 'Act 3', 'The Solaris Temple Level 1'),
    ('1_3_8_2', 'Act 3', 'The Solaris Temple Level 2'),
    ('1_3_18_2', 'Act 3', 'The Upper Sceptre of God');

UPDATE areas SET subtype = 'Town' WHERE code = '1_3_town';
-- Note: Solaris Temple Level 2 (1_3_8_2) is the only _2+ campaign zone with a waypoint; subtype left NULL.
UPDATE areas SET subtype = 'nowp' WHERE code IN (
    '1_3_2',    -- The Slums
    '1_3_6',    -- The Catacombs
    '1_3_14_2', -- The Lunaris Temple Level 2
    '1_3_17_2', -- The Archives
    '1_3_18_2'  -- The Upper Sceptre of God
);
