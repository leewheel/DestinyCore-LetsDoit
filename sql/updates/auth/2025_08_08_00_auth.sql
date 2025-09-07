-- battlenet_accounts
SET @stmt = (
  SELECT IF(
    EXISTS(
      SELECT 1
      FROM INFORMATION_SCHEMA.COLUMNS
      WHERE TABLE_SCHEMA = DATABASE()
        AND TABLE_NAME = 'battlenet_accounts'
        AND COLUMN_NAME = 'battlePayCredits'
    ),
    'SELECT "Column exists, skip";',
    'ALTER TABLE `battlenet_accounts` ADD COLUMN `battlePayCredits` int(10) unsigned NOT NULL DEFAULT 0 AFTER `LoginTicketExpiry`;'
  )
);

PREPARE s FROM @stmt;
EXECUTE s;
DEALLOCATE PREPARE s;

-- battlepay_purchases
DROP TABLE IF EXISTS `battlepay_purchases`;

CREATE TABLE `battlepay_purchases` (
  `battlenetAccountId` int(11) DEFAULT NULL,
  `realm` int(11) DEFAULT NULL,
  `characterGuid` int(11) DEFAULT NULL,
  `productID` int(11) DEFAULT NULL,
  `productName` varchar(255) DEFAULT NULL,
  `CurrentPrice` int(11) DEFAULT NULL,
  `RemoteAddress` varchar(50) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=latin1;
