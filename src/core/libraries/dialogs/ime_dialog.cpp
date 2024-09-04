// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "ime_dialog.h"

namespace Libraries::ImeDialog {

static OrbisImeDialogStatus g_ime_dlg_status = OrbisImeDialogStatus::ORBIS_IME_DIALOG_STATUS_NONE;

int PS4_SYSV_ABI sceImeDialogAbort() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogForceClose() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogForTestFunction() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogGetCurrentStarState() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogGetPanelPositionAndForm() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogGetPanelSize() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogGetPanelSizeExtended() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogGetResult(OrbisImeDialogResult* result) {
    result->endstatus = OrbisImeDialogEndStatus::ORBIS_IME_DIALOG_END_STATUS_OK;
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogGetStatus() {
    if (g_ime_dlg_status == OrbisImeDialogStatus::ORBIS_IME_DIALOG_STATUS_RUNNING) {
        return OrbisImeDialogStatus::ORBIS_IME_DIALOG_STATUS_FINISHED;
    }
    return g_ime_dlg_status;
}

int PS4_SYSV_ABI sceImeDialogInit(OrbisImeDialogParam* param, OrbisImeParamExtended* extended) {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    const std::wstring_view text = L"shadPS4";
    param->maxTextLength = text.size();
    std::memcpy(param->inputTextBuffer, text.data(), text.size() * sizeof(wchar_t));
    g_ime_dlg_status = OrbisImeDialogStatus::ORBIS_IME_DIALOG_STATUS_RUNNING;
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogInitInternal() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogInitInternal2() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogInitInternal3() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogSetPanelPosition() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceImeDialogTerm() {
    LOG_ERROR(Lib_ImeDialog, "(STUBBED) called");
    g_ime_dlg_status = OrbisImeDialogStatus::ORBIS_IME_DIALOG_STATUS_NONE;
    return ORBIS_OK;
}

void RegisterlibSceImeDialog(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("oBmw4xrmfKs", "libSceImeDialog", 1, "libSceImeDialog", 1, 1, sceImeDialogAbort);
    LIB_FUNCTION("bX4H+sxPI-o", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogForceClose);
    LIB_FUNCTION("UFcyYDf+e88", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogForTestFunction);
    LIB_FUNCTION("fy6ntM25pEc", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogGetCurrentStarState);
    LIB_FUNCTION("8jqzzPioYl8", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogGetPanelPositionAndForm);
    LIB_FUNCTION("wqsJvRXwl58", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogGetPanelSize);
    LIB_FUNCTION("CRD+jSErEJQ", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogGetPanelSizeExtended);
    LIB_FUNCTION("x01jxu+vxlc", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogGetResult);
    LIB_FUNCTION("IADmD4tScBY", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogGetStatus);
    LIB_FUNCTION("NUeBrN7hzf0", "libSceImeDialog", 1, "libSceImeDialog", 1, 1, sceImeDialogInit);
    LIB_FUNCTION("KR6QDasuKco", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogInitInternal);
    LIB_FUNCTION("oe92cnJQ9HE", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogInitInternal2);
    LIB_FUNCTION("IoKIpNf9EK0", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogInitInternal3);
    LIB_FUNCTION("-2WqB87KKGg", "libSceImeDialog", 1, "libSceImeDialog", 1, 1,
                 sceImeDialogSetPanelPosition);
    LIB_FUNCTION("gyTyVn+bXMw", "libSceImeDialog", 1, "libSceImeDialog", 1, 1, sceImeDialogTerm);
};

} // namespace Libraries::ImeDialog