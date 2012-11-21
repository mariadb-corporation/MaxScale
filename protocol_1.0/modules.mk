mod_skysql.la: mod_skysql.slo skysql_utils.slo skysql_backend.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_skysql.lo skysql_utils.lo skysql_backend.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_skysql.la
