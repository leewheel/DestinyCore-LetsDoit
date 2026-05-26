-- SmartWanderGenerator facing-continuity bias
--
-- Adds the facing weight: candidates lying along the creature's current
-- heading score higher than candidates requiring a 180° pivot. Soft bias —
-- the creature still turns when the forward candidates are all bad, just
-- doesn't snap-turn for no reason between two consecutive splines.

ALTER TABLE `wander_profile`
    ADD COLUMN `WeightFacing` FLOAT NOT NULL DEFAULT 0.4 AFTER `WeightDensity`;

-- Tune per stock profile. Guards / idle NPCs don't mind facing changes much;
-- wandering wildlife benefits the most (they pace, not pivot).
UPDATE `wander_profile` SET `WeightFacing` = 0.3 WHERE `Id` = 1; -- guard_urban
UPDATE `wander_profile` SET `WeightFacing` = 0.6 WHERE `Id` = 2; -- wildlife_skittish
UPDATE `wander_profile` SET `WeightFacing` = 0.4 WHERE `Id` = 3; -- fish_aquatic
UPDATE `wander_profile` SET `WeightFacing` = 0.2 WHERE `Id` = 4; -- idle_npc (small range = pivots ok)
