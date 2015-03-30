SUBDIRS = src man

if WITH_SYSTEMD
EXTRA_DIST = data/keyledd.service
systemd_unitsdir = $(prefix)/lib/systemd/system/
systemd_units_DATA = data/keyledd.service
endif WITH_SYSTEMD
