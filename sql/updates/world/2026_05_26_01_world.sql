-- SmartWanderGenerator
--
-- Adds a profile-driven utility-AI wander system. Each row in `wander_profile`
-- defines weights + sampler config; `creature_template_addon.WanderProfileId`
-- references one of those rows. Id 0 (or NULL / unset) means "use the C++
-- built-in default" (SmartWander::Profile::Default()).

DROP TABLE IF EXISTS `wander_profile`;
CREATE TABLE `wander_profile` (
    `Id`              INT UNSIGNED   NOT NULL,
    `Name`            VARCHAR(64)    NOT NULL DEFAULT '',
    `SamplerMode`     TINYINT UNSIGNED NOT NULL DEFAULT 0   COMMENT '0=polar random, 1=Halton low-discrepancy',
    `SampleCount`     TINYINT UNSIGNED NOT NULL DEFAULT 8,
    `TopK`            TINYINT UNSIGNED NOT NULL DEFAULT 3,
    `MemorySize`      TINYINT UNSIGNED NOT NULL DEFAULT 5,
    `MinRadius`       FLOAT          NOT NULL DEFAULT 3.0,
    `MaxRadius`       FLOAT          NOT NULL DEFAULT 0.0   COMMENT '0 = use Creature::GetRespawnRadius()',
    `TickMsMin`       INT UNSIGNED   NOT NULL DEFAULT 4000,
    `TickMsMax`       INT UNSIGNED   NOT NULL DEFAULT 8000,
    `WeightDistance`  FLOAT          NOT NULL DEFAULT 1.0,
    `WeightSlope`     FLOAT          NOT NULL DEFAULT 1.0,
    `WeightNavmesh`   FLOAT          NOT NULL DEFAULT 0.7,
    `WeightWater`     FLOAT          NOT NULL DEFAULT 0.0,
    `WeightMemory`    FLOAT          NOT NULL DEFAULT 0.5,
    `WeightLosSpawn`  FLOAT          NOT NULL DEFAULT 0.0,
    PRIMARY KEY (`Id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- Stock profiles. Id 0 is reserved (built-in default), so we start at 1.
--
-- 1 guard_urban       Short range, biases toward keeping LOS to spawn.
-- 2 wildlife_skittish Medium range, strong memory (don't loop), no water.
-- 3 fish_aquatic      Strong water affinity, requires CanSwim.
-- 4 idle_npc          Very short range, max memory — barely moves.
INSERT INTO `wander_profile`
    (`Id`,`Name`,`SamplerMode`,`SampleCount`,`TopK`,`MemorySize`,`MinRadius`,`MaxRadius`,`TickMsMin`,`TickMsMax`,`WeightDistance`,`WeightSlope`,`WeightNavmesh`,`WeightWater`,`WeightMemory`,`WeightLosSpawn`) VALUES
    (1,'guard_urban',       0, 10, 2, 4,  2.0,  8.0, 5000, 12000, 1.0, 1.2, 0.8, 0.0, 0.3, 2.0),
    (2,'wildlife_skittish', 0,  8, 3, 6,  3.0, 18.0, 3500,  8000, 1.0, 1.0, 0.7, 0.0, 1.0, 0.0),
    (3,'fish_aquatic',      0,  8, 3, 5,  3.0, 12.0, 4000, 10000, 0.8, 0.4, 0.5, 1.5, 0.5, 0.0),
    (4,'idle_npc',          0,  6, 2, 8,  1.0,  3.0, 6000, 15000, 0.6, 0.6, 0.3, 0.0, 1.5, 0.0);

-- Extend creature_template_addon with the opt-in profile reference.
-- 0 / NULL = use the built-in default; >0 = WanderProfileMgr::GetProfile(id).
ALTER TABLE `creature_template_addon`
    ADD COLUMN `WanderProfileId` INT UNSIGNED NOT NULL DEFAULT 0 AFTER `visibilityDistanceType`;
