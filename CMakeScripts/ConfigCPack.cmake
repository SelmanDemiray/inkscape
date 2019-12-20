############################
# CPack configuration file #
############################


## General ##
set(CPACK_PACKAGE_NAME "Inkscape")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Open-source vector graphics editor")
set(CPACK_PACKAGE_VENDOR "Inkscape")
set(CPACK_PACKAGE_CONTACT "Inkscape developers <inkscape-devel@lists.sourceforge.net>")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://inkscape.org")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSES/GPL-3.0.txt")
set(CPACK_RESOURCE_FILE_README "${CMAKE_SOURCE_DIR}/README.md")
set(CPACK_PACKAGE_VERSION_MAJOR ${INKSCAPE_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${INKSCAPE_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${INKSCAPE_VERSION_PATCH})
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/share/branding/inkscape.ico")

set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}~${INKSCAPE_VERSION_SUFFIX}")
set(CPACK_SOURCE_IGNORE_FILES "~$;[.]swp$;/[.]svn/;/[.]git/;.gitignore;/build/;/obj*/;cscope.*;.gitlab*;.coveragerc;*.md;")
set(INKSCAPE_CPACK_PREFIX ${PROJECT_NAME}-${INKSCAPE_VERSION}_${INKSCAPE_REVISION_DATE}_${INKSCAPE_REVISION_HASH})
set(CPACK_SOURCE_PACKAGE_FILE_NAME ${INKSCAPE_CPACK_PREFIX})
set(CPACK_PACKAGE_FILE_NAME ${INKSCAPE_CPACK_PREFIX})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "inkscape")
set(CPACK_SOURCE_GENERATOR "TXZ")
set(CPACK_PACKAGE_EXECUTABLES "inkscape;Inkscape;inkview;Inkview")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_WARN_ON_ABSOLUTE_INSTALL_DESTINATION TRUE)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/CMakeScripts/CPack.cmake")

configure_file("${CMAKE_SOURCE_DIR}/CMakeScripts/CPack.cmake" "${CMAKE_BINARY_DIR}/CMakeScripts/CPack.cmake" @ONLY)



## Generator-specific configuration ##

# NSIS (Windows .exe installer)
set(CPACK_NSIS_DISPLAY_NAME "Inkscape")
set(CPACK_NSIS_COMPRESSOR "/SOLID zlib")
set(CPACK_NSIS_MENU_LINKS "https://inkscape.org/" "Inkscape homepage")

# WIX (Windows .msi installer)
set(CPACK_WIX_UPGRADE_GUID "4d5fedaa-84a0-48be-bd2a-08246398361a")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/share/branding/inkscape.ico")
set(CPACK_WIX_UI_BANNER  "${CMAKE_SOURCE_DIR}/packaging/wix/Bitmaps/banner.bmp")
set(CPACK_WIX_UI_DIALOG  "${CMAKE_SOURCE_DIR}/packaging/wix/Bitmaps/dialog.bmp")

# DEB (Linux .deb bundle)
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libaspell15 (>= 0.60.7~20110707), libatkmm-1.6-1v5 (>= 2.24.0), libc6 (>= 2.14), libcairo2 (>= 1.14.0), libcairomm-1.0-1v5 (>= 1.12.0), libcdr-0.1-1, libdbus-glib-1-2 (>= 0.88), libfontconfig1 (>= 2.12), libfreetype6 (>= 2.2.1), libgc1c2 (>= 1:7.2d), libgcc1 (>= 1:4.0), libgdk-pixbuf2.0-0 (>= 2.22.0), libgdl-3-5 (>= 3.8.1), libglib2.0-0 (>= 2.41.1), libglibmm-2.4-1v5 (>= 2.54.0), libgomp1 (>= 4.9), libgsl23, libgslcblas0, libgtk-3-0 (>= 3.21.5), libgtkmm-3.0-1v5 (>= 3.22.0), libgtkspell3-3-0, libharfbuzz0b (>= 1.2.6), libjpeg8 (>= 8c), liblcms2-2 (>= 2.2+git20110628), libmagick++-6.q16-7 (>= 8:6.9.6.8), libpango-1.0-0 (>= 1.37.2), libpangocairo-1.0-0 (>= 1.14.0), libpangoft2-1.0-0 (>= 1.37.2), libpangomm-1.4-1v5 (>= 2.40.0), libpng16-16 (>= 1.6.2-1), libpoppler-glib8 (>= 0.18.0), libpoppler68 (>= 0.57.0), libpotrace0, librevenge-0.0-0, libsigc++-2.0-0v5 (>= 2.8.0), libsoup2.4-1 (>= 2.41.90), libstdc++6 (>= 5.2), libvisio-0.1-1, libwpg-0.3-3, libx11-6, libxml2 (>= 2.7.4), libxslt1.1 (>= 1.1.25), zlib1g (>= 1:1.1.4)")
SET(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
SET(CPACK_DEBIAN_PACKAGE_RECOMMENDS "aspell, imagemagick, libwmf-bin, perlmagick, python-numpy, python-lxml, python-scour, python-uniconvertor")
SET(CPACK_DEBIAN_PACKAGE_SUGGESTS "dia, libsvg-perl, libxml-xql-perl, pstoedit, python-uniconvertor, ruby")




## load cpack module (do this *after* all the CPACK_* variables have been set)

include(CPack)