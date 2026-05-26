-- SmartWanderGenerator anti-clumping
--
-- Adds the density weight column to wander_profile and tunes the stock
-- profiles to actually use it. Density score is the WanderInfluenceMap-driven
-- penalty applied to candidates whose 8m cell is already claimed by other
-- smart-wanderers.

ALTER TABLE `wander_profile`
    ADD COLUMN `WeightDensity` FLOAT NOT NULL DEFAULT 0.5 AFTER `WeightLosSpawn`;

-- Tune stock profiles for the new criterion. Guards still allow tight
-- clusters (low density weight), wildlife/idle benefit most from spreading.
UPDATE `wander_profile` SET `WeightDensity` = 0.2 WHERE `Id` = 1; -- guard_urban
UPDATE `wander_profile` SET `WeightDensity` = 1.0 WHERE `Id` = 2; -- wildlife_skittish
UPDATE `wander_profile` SET `WeightDensity` = 0.4 WHERE `Id` = 3; -- fish_aquatic (school loosely)
UPDATE `wander_profile` SET `WeightDensity` = 0.6 WHERE `Id` = 4; -- idle_npc
