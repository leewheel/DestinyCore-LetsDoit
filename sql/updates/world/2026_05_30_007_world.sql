-- BlizzLike : MinPlayersPerTeam/MaxPlayersPerTeam/MinLvl/MaxLvl proviennent de BattlemasterList.db2
-- (lus via sBattlemasterListStore dans BattlegroundMgr::LoadBattlegroundTemplates). Les valeurs
-- custom heritees de DestinyCore en base sont ecartees.
ALTER TABLE `battleground_template`
  DROP COLUMN `MinPlayersPerTeam`,
  DROP COLUMN `MaxPlayersPerTeam`,
  DROP COLUMN `MinLvl`,
  DROP COLUMN `MaxLvl`;
