# SPDX-FileCopyrightText: 2026 Samer Ali
# SPDX-License-Identifier: GPL-3.0-only

ironsmith_extend_target(CorePlugin
        SOURCES
            IUiHost.hpp
            UiHostImpl.hpp
            UiHostImpl.cpp
            UiStyle.hpp
            IconLoader.hpp
            IconLoader.cpp
            CoreUi.qrc
)

if (TARGET Qt6::Svg)
    ironsmith_extend_target(CorePlugin
            PUBLIC_DEPENDS
                Qt6::Svg
            PRIVATE_DEFINES
                CORE_HAVE_QT_SVG=1
    )
else ()
    ironsmith_extend_target(CorePlugin
            PRIVATE_DEFINES
                CORE_HAVE_QT_SVG=0
    )
endif ()
