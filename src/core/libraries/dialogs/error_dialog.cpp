// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/libs.h"
#include "error_codes.h"
#include "error_dialog.h"

namespace Libraries::ErrorDialog {

static OrbisErrorDialogStatus g_error_dlg_status =
    OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_NONE;

int PS4_SYSV_ABI sceErrorDialogClose() {
    g_error_dlg_status = OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_FINISHED;
    return ORBIS_OK;
}

OrbisErrorDialogStatus PS4_SYSV_ABI sceErrorDialogGetStatus() {
    return g_error_dlg_status;
}

int PS4_SYSV_ABI sceErrorDialogInitialize(OrbisErrorDialogParam* param) {
    if (g_error_dlg_status == OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_INITIALIZED) {
        LOG_ERROR(Lib_ErrorDialog, "Error dialog is already at init mode");
        return ORBIS_ERROR_DIALOG_ERROR_ALREADY_INITIALIZED;
    }
    g_error_dlg_status = OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_INITIALIZED;
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceErrorDialogOpen(OrbisErrorDialogParam* param) {
    LOG_ERROR(Lib_ErrorDialog, "size = {} errorcode = {:#x} userid = {}", param->size,
              param->errorCode, param->userId);
    g_error_dlg_status = OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_RUNNING;
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceErrorDialogOpenDetail() {
    LOG_ERROR(Lib_ErrorDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceErrorDialogOpenWithReport() {
    LOG_ERROR(Lib_ErrorDialog, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceErrorDialogTerminate() {
    if (g_error_dlg_status == OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_NONE) {
        LOG_ERROR(Lib_ErrorDialog, "Error dialog hasn't initialized");
        return ORBIS_ERROR_DIALOG_ERROR_NOT_INITIALIZED;
    }
    g_error_dlg_status = OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_NONE;
    return ORBIS_OK;
}

OrbisErrorDialogStatus PS4_SYSV_ABI sceErrorDialogUpdateStatus() {
    // TODO when imgui dialog is done this will loop until ORBIS_ERROR_DIALOG_STATUS_FINISHED
    // This should be done calling sceErrorDialogClose but since we don't have a dialog we finish it
    // here
    return OrbisErrorDialogStatus::ORBIS_ERROR_DIALOG_STATUS_FINISHED;
}

void RegisterlibSceErrorDialog(Core::Loader::SymbolsResolver* sym) {
    LIB_FUNCTION("ekXHb1kDBl0", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogClose);
    LIB_FUNCTION("t2FvHRXzgqk", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogGetStatus);
    LIB_FUNCTION("I88KChlynSs", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogInitialize);
    LIB_FUNCTION("M2ZF-ClLhgY", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogOpen);
    LIB_FUNCTION("jrpnVQfJYgQ", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogOpenDetail);
    LIB_FUNCTION("wktCiyWoDTI", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogOpenWithReport);
    LIB_FUNCTION("9XAxK2PMwk8", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogTerminate);
    LIB_FUNCTION("WWiGuh9XfgQ", "libSceErrorDialog", 1, "libSceErrorDialog", 1, 1,
                 sceErrorDialogUpdateStatus);
};

} // namespace Libraries::ErrorDialog