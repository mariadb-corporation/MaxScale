CREATE TABLE `canonical_queries` (
  `id` mediumint(8) unsigned NOT NULL AUTO_INCREMENT,
  `hash` bigint(20) unsigned NOT NULL,
  `canonicalQuery` varchar(2048) COLLATE utf8_unicode_ci NOT NULL,
  `count` int unsigned NOT NULL DEFAULT '1',
  `createdAt` timestamp NOT NULL DEFAULT '0000-00-00 00:00:00',
  `updatedAt` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (`hash`),
  KEY `indx_id` (`id`) USING BTREE
) ENGINE=myISAM DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;