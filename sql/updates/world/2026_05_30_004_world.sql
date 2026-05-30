-- Vestige 3.3.5 : les spells sont charges depuis Spell.db2/SpellMisc.db2 (sSpell*Store) + hotfixes.
-- Cette table n'est lue par aucune requete du core 7.3.5. Les spells custom d'epoque qui y
-- vivaient (kill-credits de boss, etc.) sont deja inactifs ; les conserver implique de les
-- migrer vers la DB hotfixes au prealable.
DROP TABLE IF EXISTS `spell_dbc`;
