-- Vestige 3.3.5 : les achievements sont charges depuis Achievement.db2 (sAchievementStore)
-- et la DB hotfixes. Cette table n'est lue par aucune requete du core 7.3.5.
DROP TABLE IF EXISTS `achievement_dbc`;
