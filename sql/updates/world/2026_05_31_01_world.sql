INSERT INTO `quest_action` (`QuestID`, `Type`, `ObjectiveIndex`, `SpellId`, `ConversationId`, `UpdatePhaseShift`, `UpdateZoneAuras`)
SELECT `questId`, 0, 0, 0, `questAcceptConversation`, 0, 0 FROM `quest_conversation` WHERE `questAcceptConversation` > 0
ON DUPLICATE KEY UPDATE `ConversationId` = VALUES(`ConversationId`);

INSERT INTO `quest_action` (`QuestID`, `Type`, `ObjectiveIndex`, `SpellId`, `ConversationId`, `UpdatePhaseShift`, `UpdateZoneAuras`)
SELECT `questId`, 2, 0, 0, `questCompleteConversation`, 0, 0 FROM `quest_conversation` WHERE `questCompleteConversation` > 0
ON DUPLICATE KEY UPDATE `ConversationId` = VALUES(`ConversationId`);

DROP TABLE IF EXISTS `quest_conversation`;
