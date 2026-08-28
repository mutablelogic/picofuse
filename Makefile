# Installation prefix and build directory
BUILD_DIR ?= build
PREFIX ?= $(BUILD_DIR)
CMAKE_BUILD_TYPE ?= Release

# Tools
CMAKE ?= $(shell which cmake 2>/dev/null)
DOCKER ?= $(shell which docker 2>/dev/null)
GIT ?= $(shell which git 2>/dev/null)

# Determine the program version from git tags or commit hash
PROGRAM_VERSION ?= $(shell \
	if test -x "${GIT}"; then \
		${GIT} describe --tags --exact-match 2>/dev/null || \
		${GIT} rev-parse --short HEAD 2>/dev/null; \
	fi)
VERSION_NUMBER := $(if $(strip ${PROGRAM_VERSION}),${PROGRAM_VERSION},0.0.0)

###############################################################################
# CONFIGURE AND BUILD

.PHONY: build
build: configure
	@${CMAKE} --build ${BUILD_DIR} --target all -j 8

.PHONY: configure
configure: dep-cmake submodule
	@${CMAKE} -B ${BUILD_DIR} \
		-D CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} \
		$(if ${PROGRAM_VERSION},-D PROGRAM_VERSION=${PROGRAM_VERSION}) \
		$(if ${PICO_BOARD},-D PICO_BOARD=${PICO_BOARD})

.PHONY: test
test: build
	@${CMAKE} --build ${BUILD_DIR} --target test

.PHONY: version
version:
	@echo ${VERSION_NUMBER}

.PHONY: submodule
submodule: dep-git
	@${GIT} submodule update --init --recursive

###############################################################################
# INSTALL

.PHONY: install
install: build
	@${CMAKE} --install ${BUILD_DIR} --prefix ${PREFIX}

###############################################################################
# CLEAN

.PHONY: clean
clean:
	@rm -rf ${BUILD_DIR}

###############################################################################
# DOCUMENTATION

.PHONY: doc
doc: dep-docker 
	@echo
	@echo make doc
	@${DOCKER} run -v .:/data greenbone/doxygen doxygen /data/doxygen/Doxyfile

###############################################################################
# DEPENDENCIES

.PHONY: dep-cmake
dep-cmake:
	@test -f "${CMAKE}" && test -x "${CMAKE}" || (echo "Missing CMAKE: ${CMAKE}" && exit 1)

.PHONY: dep-docker
dep-docker:
	@test -f "${DOCKER}" && test -x "${DOCKER}" || (echo "Missing DOCKER: ${DOCKER}" && exit 1)

.PHONY: dep-git
dep-git:
	@test -f "${GIT}" && test -x "${GIT}" || (echo "Missing GIT: ${GIT}" && exit 1)
