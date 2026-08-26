ACLOCAL_AMFLAGS = -I m4

SUBDIRS = \
	deps \
	src

if BUILD_RACE
SUBDIRS += share
endif

html:
	doxygen

html-clean:
	rm -rf doc/html

html-release:
	rsync -rz --delete doc/html/ jaydolan.com:/var/www/projects/quetoo

.PHONY: install-tools
install-tools:
	$(MAKE) -C src install-tools
