; ================================================================
; installer.nsi — NSIS інсталятор для SSH Key Copy
;
; Що робить:
;   1. Встановлює ssh_key_copy.exe у Program Files
;   2. Додає root_ca.crt до сховища довірених кореневих CA
;   3. Створює ярлики у меню Пуск і на робочому столі
;   4. Реєструє деінсталятор у "Програми та компоненти"
;   5. Асоціює .conf файли з програмою
;
; Збірка:
;   makensis installer.nsi
;   або: makensis -V4 installer.nsi  (детальний вивід)
;
; Встановлення NSIS:
;   Windows: https://nsis.sourceforge.io/Download
;   Linux:   sudo apt install nsis
;   macOS:   brew install nsis
;
; Необхідні файли у тій самій папці:
;   ssh_key_copy.exe  — скомпільована програма
;   root_ca.crt       — кореневий сертифікат (з sign_selfsigned.sh)
;   ssh-copy-id.ico   — іконка
; ================================================================

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!insertmacro GetSize

; ── Метадані ────────────────────────────────────────────────
!define PRODUCT_NAME        "SSH Key Copy"
!define PRODUCT_VERSION     "1.0.0"
!define PRODUCT_PUBLISHER   "MyOrganization"
!define PRODUCT_URL         "https://github.com/your-org/ssh-key-copy"
!define PRODUCT_EXE         "ssh_key_copy.exe"
!define PRODUCT_ICON        "ssh-copy-id.ico"
!define CERT_FILE           "root_ca.crt"
!define INSTALL_DIR         "$PROGRAMFILES64\SSH Key Copy"
!define REG_UNINSTALL       "Software\Microsoft\Windows\CurrentVersion\Uninstall\SSHKeyCopy"
!define REG_APP             "Software\SSHKeyCopy"

; ── Налаштування інсталятора ─────────────────────────────────
Name                "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile             "SSHKeyCopy_Setup_${PRODUCT_VERSION}.exe"
InstallDir          "${INSTALL_DIR}"
InstallDirRegKey    HKLM "${REG_APP}" "InstallPath"
RequestExecutionLevel admin
SetCompressor       /SOLID lzma
SetCompressorDictSize 64

; ── Версія (для Properties у Explorer) ───────────────────────
VIProductVersion    "${PRODUCT_VERSION}.0"
VIAddVersionKey     "ProductName"      "${PRODUCT_NAME}"
VIAddVersionKey     "ProductVersion"   "${PRODUCT_VERSION}"
VIAddVersionKey     "CompanyName"      "${PRODUCT_PUBLISHER}"
VIAddVersionKey     "FileDescription"  "SSH Key Copy Installer"
VIAddVersionKey     "FileVersion"      "${PRODUCT_VERSION}"
VIAddVersionKey     "LegalCopyright"   "© 2025 ${PRODUCT_PUBLISHER}"

; ── Вигляд (Modern UI) ───────────────────────────────────────
!define MUI_ICON              "${PRODUCT_ICON}"
!define MUI_UNICON            "${PRODUCT_ICON}"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TITLE "Встановлення ${PRODUCT_NAME}"
!define MUI_WELCOMEPAGE_TEXT  "Цей майстер встановить ${PRODUCT_NAME} ${PRODUCT_VERSION} на ваш комп'ютер.$\r$\n$\r$\nПрограма дозволяє передавати SSH-ключі на Unix/Linux сервери.$\r$\n$\r$\nНатисніть «Далі» щоб продовжити."
!define MUI_FINISHPAGE_RUN    "$INSTDIR\${PRODUCT_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Запустити ${PRODUCT_NAME}"
!define MUI_FINISHPAGE_SHOWREADME ""
!define MUI_FINISHPAGE_LINK   "${PRODUCT_URL}"
!define MUI_FINISHPAGE_LINK_LOCATION "${PRODUCT_URL}"

; Сторінки інсталятора
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

; Сторінки деінсталятора
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Мова
!insertmacro MUI_LANGUAGE "Ukrainian"
!insertmacro MUI_LANGUAGE "English"

; ================================================================
;  СЕКЦІЯ ВСТАНОВЛЕННЯ
; ================================================================
Section "Основні файли" SEC_MAIN
    SectionIn RO  ; обов'язковий, не можна зняти

    SetOutPath "$INSTDIR"
    SetOverwrite on

    ; ── 1. Копіюємо файли програми ──────────────────────────
    File "${PRODUCT_EXE}"
    File "${PRODUCT_ICON}"
    File /nonfatal "${CERT_FILE}"   ; /nonfatal — не падати якщо немає

    ; ── 2. Встановлення сертифікату ─────────────────────────
    ${If} ${FileExists} "$INSTDIR\${CERT_FILE}"
        DetailPrint "Встановлення кореневого сертифікату..."
        ; certutil -addstore -f "Root" — додає без діалогів, примусово
        nsExec::ExecToLog 'certutil -addstore -f "Root" "$INSTDIR\${CERT_FILE}"'
        Pop $0
        ${If} $0 == 0
            DetailPrint "Сертифікат успішно встановлено."
        ${Else}
            DetailPrint "Попередження: не вдалося встановити сертифікат (код $0)."
            DetailPrint "Встановіть вручну: certutil -addstore Root root_ca.crt"
        ${EndIf}
    ${Else}
        DetailPrint "Файл ${CERT_FILE} не знайдено — сертифікат не встановлено."
    ${EndIf}

    ; ── 3. Ярлик у меню Пуск ────────────────────────────────
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" \
                    "$INSTDIR\${PRODUCT_EXE}" "" \
                    "$INSTDIR\${PRODUCT_ICON}" 0

    CreateShortcut  "$SMPROGRAMS\${PRODUCT_NAME}\Видалити ${PRODUCT_NAME}.lnk" \
                    "$INSTDIR\uninstall.exe"

    ; ── 4. Ярлик на робочому столі ──────────────────────────
    CreateShortcut  "$DESKTOP\${PRODUCT_NAME}.lnk" \
                    "$INSTDIR\${PRODUCT_EXE}" "" \
                    "$INSTDIR\${PRODUCT_ICON}" 0

    ; ── 5. Асоціація .conf файлів з програмою ───────────────
    WriteRegStr HKCR ".conf\OpenWithProgids" "SSHKeyCopy.conf" ""
    WriteRegStr HKCR "SSHKeyCopy.conf" "" "SSH Key Copy Config"
    WriteRegStr HKCR "SSHKeyCopy.conf\DefaultIcon" "" "$INSTDIR\${PRODUCT_ICON},0"
    WriteRegStr HKCR "SSHKeyCopy.conf\shell\open\command" "" \
                '"$INSTDIR\${PRODUCT_EXE}" "%1"'

    ; Оновити асоціації у Explorer
    System::Call 'shell32::SHChangeNotify(i 0x8000000, i 0, i 0, i 0)'

    ; ── 6. Реєстрація у "Програми та компоненти" ────────────
    WriteRegStr   HKLM "${REG_UNINSTALL}" "DisplayName"      "${PRODUCT_NAME}"
    WriteRegStr   HKLM "${REG_UNINSTALL}" "DisplayVersion"   "${PRODUCT_VERSION}"
    WriteRegStr   HKLM "${REG_UNINSTALL}" "Publisher"        "${PRODUCT_PUBLISHER}"
    WriteRegStr   HKLM "${REG_UNINSTALL}" "URLInfoAbout"     "${PRODUCT_URL}"
    WriteRegStr   HKLM "${REG_UNINSTALL}" "DisplayIcon"      "$INSTDIR\${PRODUCT_ICON}"
    WriteRegStr   HKLM "${REG_UNINSTALL}" "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKLM "${REG_UNINSTALL}" "UninstallString"  '"$INSTDIR\uninstall.exe"'
    WriteRegStr   HKLM "${REG_UNINSTALL}" "QuietUninstallString" \
                  '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "NoModify"         1
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "NoRepair"         1

    ; Розмір інсталяції (КБ)
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "${REG_UNINSTALL}" "EstimatedSize" "$0"

    ; Шлях встановлення
    WriteRegStr HKLM "${REG_APP}" "InstallPath" "$INSTDIR"
    WriteRegStr HKLM "${REG_APP}" "Version"     "${PRODUCT_VERSION}"

    ; ── 7. Генерація деінсталятора ──────────────────────────
    WriteUninstaller "$INSTDIR\uninstall.exe"

SectionEnd

; ================================================================
;  СЕКЦІЯ ДЕІНСТАЛЯЦІЇ
; ================================================================
Section "Uninstall"

    ; ── Видаляємо сертифікат ────────────────────────────────
    DetailPrint "Видалення сертифікату зі сховища..."
    ; Знаходимо сертифікат за CN і видаляємо
    nsExec::ExecToLog 'certutil -delstore "Root" "SSH Key Copy"'
    nsExec::ExecToLog 'certutil -delstore "Root" "MyOrganization Root CA"'

    ; ── Видаляємо файли ─────────────────────────────────────
    Delete "$INSTDIR\${PRODUCT_EXE}"
    Delete "$INSTDIR\${PRODUCT_ICON}"
    Delete "$INSTDIR\${CERT_FILE}"
    Delete "$INSTDIR\uninstall.exe"
    RMDir  "$INSTDIR"

    ; ── Видаляємо ярлики ────────────────────────────────────
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk"
    Delete "$SMPROGRAMS\${PRODUCT_NAME}\Видалити ${PRODUCT_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${PRODUCT_NAME}"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"

    ; ── Видаляємо асоціації файлів ──────────────────────────
    DeleteRegKey HKCR "SSHKeyCopy.conf"
    DeleteRegValue HKCR ".conf\OpenWithProgids" "SSHKeyCopy.conf"
    System::Call 'shell32::SHChangeNotify(i 0x8000000, i 0, i 0, i 0)'

    ; ── Видаляємо записи реєстру ────────────────────────────
    DeleteRegKey HKLM "${REG_UNINSTALL}"
    DeleteRegKey HKLM "${REG_APP}"

    DetailPrint "Видалення завершено."

SectionEnd

; ================================================================
;  ФУНКЦІЇ
; ================================================================

; Перевірка при запуску — чи не встановлено вже
Function .onInit
    ; Перевіряємо чи програма вже запущена
    FindWindow $0 "SSHKeyCopyWin" ""
    ${If} $0 <> 0
        MessageBox MB_OK|MB_ICONEXCLAMATION \
            "${PRODUCT_NAME} зараз запущено.$\nЗакрийте програму перед встановленням."
        Abort
    ${EndIf}

    ; Перевірка версії Windows пропущена
FunctionEnd

; Перевірка при деінсталяції
Function un.onInit
    MessageBox MB_YESNO|MB_ICONQUESTION \
        "Ви впевнені що хочете видалити ${PRODUCT_NAME}?$\r$\n$\r$\nБуде також видалено кореневий сертифікат." \
        IDYES +2
    Abort
FunctionEnd
