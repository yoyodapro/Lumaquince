# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# SPDX-FileCopyrightText: 2019 yuzu Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later

# Neutral release metadata; never embed a local branch name or private build tag.
string(TIMESTAMP BUILD_DATE UTC)
set(GIT_COMMIT "source-package")
set(GIT_REFSPEC "preview")
set(BUILD_VERSION "0.1.0-preview.1")
set(GIT_DESC "${BUILD_VERSION}")
set(IS_DEV_BUILD false)
set(IS_NIGHTLY_BUILD false)
set(REPO_NAME "Lumaquince")
set(BUILD_ID "0.1.0-preview.1")
set(BUILD_FULLNAME "Lumaquince 0.1.0 Preview 1")
set(TITLE_BAR_FORMAT_IDLE "Lumaquince 0.1.0 Preview 1")
set(TITLE_BAR_FORMAT_RUNNING "Lumaquince 0.1.0 Preview 1")
set(LICHII_BUILD_TAG "")
set(LICHII_BUILD_PLATFORM "")
set(BUILD_AUTO_UPDATE_STABLE_REPO "")
set(BUILD_AUTO_UPDATE_STABLE_API "")
set(BUILD_AUTO_UPDATE_API_PATH "")
set(BUILD_AUTO_UPDATE_WEBSITE "")
set(BUILD_AUTO_UPDATE_REPO "")
set(CXX_COMPILER "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
configure_file(scm_rev.cpp.in scm_rev.cpp @ONLY)
