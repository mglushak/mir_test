# ================================================================
# Makefile — SSH Key Copy
# ================================================================
# make mac      — зібрати для macOS (clang, тільки на Mac)
# make windows  — зібрати для Windows (MinGW-w64, з будь-якого Linux)
# make icon     — конвертувати PNG -> ICO (ImageMagick)
# make clean    — прибрати артефакти
# ================================================================

ICON_PNG  = ssh-copy-id.png
ICON_ICO  = ssh-copy-id.ico

# ----- macOS -----
MAC_SRC   = ssh_key_copy_mac.m
MAC_OUT   = ssh_key_copy
MAC_CC    = clang
MAC_FLAGS = -x objective-c \
            -framework Cocoa \
            -framework Foundation \
            -fobjc-arc \
            -Wall -Wextra -Wno-unused-parameter \
            -O2

# ----- Windows (cross-compile з Linux) -----
WIN_SRC   = ssh_key_copy_win.c
WIN_OUT   = ssh_key_copy.exe
WIN_CC    = x86_64-w64-mingw32-gcc
WIN_RC    = x86_64-w64-mingw32-windres
WIN_FLAGS = -lws2_32 -lcomctl32 -lshell32 -mwindows -municode \
            -DUNICODE -D_UNICODE \
            -Wall -Wextra -Wno-unused-parameter \
            -O2

.PHONY: mac windows icon clean help

help:
	@echo "Використання:"
	@echo "  make mac      — зібрати macOS-бінарник (на macOS)"
	@echo "  make windows  — зібрати Windows .exe (MinGW-w64)"
	@echo "  make icon     — конвертувати PNG -> ICO (ImageMagick)"
	@echo "  make clean    — видалити артефакти"

# ---------- macOS ----------
mac: $(MAC_SRC)
	@echo "[MAC] Компіляція $(MAC_OUT)..."
	$(MAC_CC) $(MAC_FLAGS) $(MAC_SRC) -o $(MAC_OUT)
	@echo "[MAC] Готово: ./$(MAC_OUT)"

# ---------- Windows ----------
windows: $(WIN_SRC) resource.res
	@echo "[WIN] Компіляція $(WIN_OUT)..."
	$(WIN_CC) $(WIN_SRC) resource.res -o $(WIN_OUT) $(WIN_FLAGS)
	@echo "[WIN] Готово: $(WIN_OUT)"

resource.res: resource.rc
	@if [ -f $(ICON_ICO) ]; then \
	    echo "[RC]  Компіляція ресурсу (з іконкою)..."; \
	    $(WIN_RC) resource.rc -O coff -o resource.res; \
	else \
	    echo "[RC]  ICO не знайдено. Спробуйте: make icon"; \
	    echo "[RC]  Компіляція ресурсу без іконки..."; \
	    echo "// empty" | $(WIN_RC) /dev/stdin -O coff -o resource.res 2>/dev/null || \
	    touch resource.res; \
	fi

# Збірка без іконки (якщо ImageMagick недоступний)
windows-no-icon: $(WIN_SRC)
	@echo "[WIN] Компіляція без іконки..."
	$(WIN_CC) $(WIN_SRC) -o $(WIN_OUT) $(WIN_FLAGS)
	@echo "[WIN] Готово: $(WIN_OUT)"

# ---------- Іконка ----------
icon: $(ICON_PNG)
	@echo "[ICO] Конвертація $(ICON_PNG) -> $(ICON_ICO)..."
	convert $(ICON_PNG) \
	    -define icon:auto-resize="256,128,64,48,32,16" \
	    $(ICON_ICO)
	@echo "[ICO] Готово: $(ICON_ICO)"

# ---------- Інсталятор (NSIS) ----------
# sudo apt install nsis
installer: $(WIN_OUT) sign
	@echo "[NSIS] Збірка інсталятора..."
	@if [ ! -f codesign/root_ca.crt ]; then echo "[WARN] root_ca.crt не знайдено. Запустіть: ./sign_selfsigned.sh $(WIN_OUT)"; fi
	@cp -f $(WIN_OUT) ssh_key_copy.exe 2>/dev/null || true
	@cp -f codesign/root_ca.crt . 2>/dev/null || true
	makensis installer.nsi
	@echo "[NSIS] Готово: SSHKeyCopy_Setup_1.0.0.exe"

sign: $(WIN_OUT)
	@echo "[SIGN] Підписування $(WIN_OUT)..."
	@if [ -f sign_selfsigned.sh ]; then bash sign_selfsigned.sh $(WIN_OUT); \
	 else echo "[WARN] sign_selfsigned.sh не знайдено, пропускаємо підпис"; fi

# ---------- Очистка ----------
clean:
	rm -f $(WIN_OUT) $(MAC_OUT) resource.res $(ICON_ICO) root_ca.crt
	rm -rf codesign/
	@echo "[CLEAN] Готово"
