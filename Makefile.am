SUBDIRS = src

man_MANS = keyledd.1 keyledd.conf.5

MAN_SUBSTS = \
	-e 's|__version__|$(PACKAGE_VERSION)|g' \
	-e 's|__config_file_path__|$(sysconfdir)/keyledd.conf|g'

SUFFIXES = .1 .5 .man

%.1 %.5 : man/%.man
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@

EXTRA_DIST = \
	man/keyledd.man \
	man/keyledd.conf.man

CLEANFILES = $(man_MANS)

if WITH_SYSTEMD
EXTRA_DIST = data/keyledd.service
systemd_unitsdir = $(prefix)/lib/systemd/system/
systemd_units_DATA = data/keyledd.service
endif WITH_SYSTEMD
