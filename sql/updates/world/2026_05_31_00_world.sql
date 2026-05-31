DROP TABLE IF EXISTS `quest_action`;
CREATE TABLE `quest_action` (
  `QuestID` int unsigned NOT NULL DEFAULT '0',
  `Type` tinyint unsigned NOT NULL DEFAULT '0',
  `ObjectiveIndex` tinyint unsigned NOT NULL DEFAULT '0',
  `SpellId` int unsigned NOT NULL DEFAULT '0',
  `ConversationId` int unsigned NOT NULL DEFAULT '0',
  `UpdatePhaseShift` tinyint unsigned NOT NULL DEFAULT '0',
  `UpdateZoneAuras` tinyint unsigned NOT NULL DEFAULT '0',
  PRIMARY KEY (`QuestID`, `Type`, `ObjectiveIndex`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
