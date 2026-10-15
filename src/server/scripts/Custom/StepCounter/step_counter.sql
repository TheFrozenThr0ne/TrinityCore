-- TrinityCore Master / Midnight - Step Counter V3
-- Import into the CHARACTERS database.
-- Check that characters.guid is BIGINT UNSIGNED in your exact core schema.
-- Existing installations: CREATE TABLE IF NOT EXISTS does not migrate old column types.

CREATE TABLE IF NOT EXISTS `character_step_counter` (
  `guid` BIGINT UNSIGNED NOT NULL,
  `total_steps` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `daily_steps` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `weekly_steps` BIGINT UNSIGNED NOT NULL DEFAULT 0,
  `day_key` INT UNSIGNED NOT NULL DEFAULT 0,
  `week_key` INT UNSIGNED NOT NULL DEFAULT 0,
  PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `step_counter_rewards` (
  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,
  `period_type` VARCHAR(16) NOT NULL,
  `reward_type` VARCHAR(16) NOT NULL,
  `required_steps` BIGINT UNSIGNED NOT NULL,
  `item_entry` INT UNSIGNED NOT NULL DEFAULT 0,
  `amount` INT UNSIGNED NOT NULL DEFAULT 0,
  `enabled` TINYINT UNSIGNED NOT NULL DEFAULT 1,
  `message` VARCHAR(255) NOT NULL DEFAULT '',
  PRIMARY KEY (`id`),
  KEY `idx_step_rewards_enabled` (`enabled`, `period_type`, `required_steps`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS `character_step_counter_rewards` (
  `guid` BIGINT UNSIGNED NOT NULL,
  `reward_id` INT UNSIGNED NOT NULL,
  `period_key` INT UNSIGNED NOT NULL DEFAULT 0,
  `claimed_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`guid`, `reward_id`, `period_key`),
  KEY `idx_step_claim_reward` (`reward_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- Reward IDs 1001-1014 are reserved for this starter pack.
-- These item IDs refer to existing WoW items, but their availability and
-- usability on your particular Midnight build must be checked in-game.
-- Gold amounts in the C++ V3 logic are interpreted as gold units.
INSERT INTO `step_counter_rewards`
 (`id`, `period_type`, `reward_type`, `required_steps`, `item_entry`, `amount`, `enabled`, `message`)
VALUES
 (1001, 'daily',  'gold',  1000,    0,      1,  1, '1.000 Schritte! Du erhaeltst 1 Gold.'),
 (1002, 'daily',  'item',  2500,    119212, 1,  1, '2.500 Schritte! Siegerhand erhalten.'),
 (1003, 'daily',  'item',  5000,    119210, 1,  1, '5.000 Schritte! Hearthstone-Spielbrett erhalten.'),
 (1004, 'daily',  'gold',  10000,   0,      10, 1, '10.000 Schritte! Du erhaeltst 10 Gold.'),
 (1005, 'daily',  'item',  10000,   122123, 1,  1, '10.000 Schritte! Darkmoon Ring-Flinger erhalten.'),
 (1006, 'weekly', 'gold',  25000,   0,      10, 1, '25.000 Wochenschritte! Du erhaeltst 10 Gold.'),
 (1007, 'weekly', 'item',  25000,   122124, 1,  1, '25.000 Wochenschritte! Darkmoon Cannon erhalten.'),
 (1008, 'weekly', 'gold',  50000,   0,      25, 1, '50.000 Wochenschritte! Du erhaeltst 25 Gold.'),
 (1009, 'weekly', 'item',  50000,   207730, 1,  1, '50.000 Wochenschritte! Goetze von Ohnahra erhalten.'),
 (1010, 'total',  'item',  100000,  119212, 1,  1, '100.000 Gesamtschritte erreicht!'),
 (1011, 'total',  'item',  250000,  119210, 1,  1, '250.000 Gesamtschritte erreicht!'),
 (1012, 'total',  'item',  500000,  122123, 1,  1, '500.000 Gesamtschritte erreicht!'),
 (1013, 'total',  'item',  1000000, 207730, 1,  1, '1.000.000 Gesamtschritte erreicht!')
ON DUPLICATE KEY UPDATE
 `period_type` = VALUES(`period_type`),
 `reward_type` = VALUES(`reward_type`),
 `required_steps` = VALUES(`required_steps`),
 `item_entry` = VALUES(`item_entry`),
 `amount` = VALUES(`amount`),
 `enabled` = VALUES(`enabled`),
 `message` = VALUES(`message`);
