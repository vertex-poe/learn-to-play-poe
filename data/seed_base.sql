-- POE1 base classes. Ascendancy classes are not tracked here; they surface
-- as character names that point back to one of these seven bases.
INSERT OR IGNORE INTO classes (name) VALUES
    ('Duelist'),
    ('Marauder'),
    ('Ranger'),
    ('Scion'),
    ('Shadow'),
    ('Templar'),
    ('Witch');

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('KalguuranSettlersLeague', 'Mechanic', 'Kingsmarch'),
    ('Labyrinth_Airlock',       'Mechanic', 'Aspirants'' Plaza'),
    ('Menagerie_Hub',           'Mechanic', 'The Menagerie'),
    ('Delve_Main',              'Mechanic', 'Azurite Mine'),
    ('SanctumFoyer_Fellshrine', 'Mechanic', 'The Forbidden Sanctum'),
    ('HeistHub',                'Mechanic', 'The Rogue Harbour'),
    ('ChayulaLeague',           'Mechanic', 'Monastery of the Keepers'),
    ('ClassicTreasury_Cosmic',  'Mechanic', 'Voidborn Reliquary');

INSERT OR IGNORE INTO areas (code, type, display_name) VALUES
    ('HideoutMountain',      'Hideout', 'Alpine Hideout'),
    ('HideoutSlum',          'Hideout', 'Backstreet Hideout'),
    ('HideoutRuinedTemple',  'Hideout', 'Baleful Hideout'),
    ('HideoutBattleground',  'Hideout', 'Battle-scarred Hideout'),
    ('HideoutTemplarLab',    'Hideout', 'Cartographer''s Hideout'),
    ('HideoutShapersRealm',  'Hideout', 'Celestial Hideout'),
    ('HideoutBeach',         'Hideout', 'Coastal Hideout'),
    ('HideoutCoral',         'Hideout', 'Coral Hideout'),
    ('HideoutOasis',         'Hideout', 'Desert Hideout'),
    ('HideoutTwilightTemple','Hideout', 'Divided Hideout'),
    ('HideoutLibrary',       'Hideout', 'Enlightened Hideout'),
    ('HideoutMine',          'Hideout', 'Excavated Hideout'),
    ('HideoutIceberg',       'Hideout', 'Glacial Hideout'),
    ('HideoutSolaris',       'Hideout', 'Immaculate Hideout'),
    ('HideoutForest',        'Hideout', 'Lush Hideout'),
    ('HideoutBaths',         'Hideout', 'Luxurious Hideout'),
    ('HideoutGardens',       'Hideout', 'Overgrown Hideout'),
    ('HideoutCrimsonTemple', 'Hideout', 'Sanguine Hideout'),
    ('HideoutOssuary',       'Hideout', 'Skeletal Hideout'),
    ('HideoutCourts',        'Hideout', 'Stately Hideout'),
    ('HideoutLight',         'Hideout', 'Timekeeper''s Hideout'),
    ('HideoutSewer',         'Hideout', 'Undercity Hideout'),
    ('HideoutFellshrine',    'Hideout', 'Unearthed Hideout');
-- NOTE: hideout list is incomplete; unknown number of additional hideouts not yet seeded.

-- Future seed tables go here (passive_quest_sources, etc.)
