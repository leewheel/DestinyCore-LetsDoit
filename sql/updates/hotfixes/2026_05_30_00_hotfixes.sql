DROP TABLE IF EXISTS `map_difficulty_x_condition`;

CREATE TABLE `map_difficulty_x_condition` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `FailureDescription` text,
  `PlayerConditionID` int NOT NULL DEFAULT '0',
  `OrderIndex` int NOT NULL DEFAULT '0',
  `MapDifficultyID` int NOT NULL DEFAULT '0',
  `VerifiedBuild` smallint NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;

DROP TABLE IF EXISTS `map_difficulty_x_condition_locale`;

CREATE TABLE `map_difficulty_x_condition_locale` (
  `ID` int unsigned NOT NULL DEFAULT '0',
  `locale` varchar(4) NOT NULL,
  `FailureDescription_lang` text,
  `VerifiedBuild` smallint NOT NULL DEFAULT '0',
  PRIMARY KEY (`ID`,`locale`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci ROW_FORMAT=DYNAMIC;
