#
# Copyright (С) since 2019 Andrei Guluaev (Winfidonarleyan/Kargatum) https://github.com/Winfidonarleyan
# Copyright (С) since 2019+ AzerothCore <www.azerothcore.org>
# Licence MIT https://opensource.org/MIT
#

# Add cpp files
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/CFBG.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/CFBG_SC.cpp")
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/CFBG.h")

# Add scripts to script loader
AC_ADD_SCRIPT_LOADER("CFBG" "${CMAKE_CURRENT_LIST_DIR}/src/CFBGLoad.h")

# Add config file
AC_ADD_CONFIG_FILE("${CMAKE_CURRENT_LIST_DIR}/conf/CFBG.conf.dist")
