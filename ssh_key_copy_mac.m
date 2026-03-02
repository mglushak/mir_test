/*
 * ssh_key_copy_mac.m  —  macOS GUI (Cocoa / Objective-C)
 *
 * КОМПІЛЯЦІЯ (Terminal на macOS):
 *   clang -x objective-c ssh_key_copy_mac.m \
 *         -o ssh_key_copy \
 *         -framework Cocoa \
 *         -framework Foundation \
 *         -fobjc-arc
 *
 * ВИМОГИ:
 *   xcode-select --install
 *
 * ІКОНКА:
 *   Покладіть ssh-copy-id.png поряд з бінарником.
 *
 * ПАРОЛЬ (автоматично):
 *   brew install hudochenkov/sshpass/sshpass
 *   Якщо sshpass відсутній — ssh запитає пароль вручну.
 */

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#include <unistd.h>
#include <sys/wait.h>
#include <locale.h>

/* ================================================================== */
/*  AppDelegate                                                        */
/* ================================================================== */
@interface AppDelegate : NSObject <NSApplicationDelegate>

@property (strong) NSWindow           *window;
@property (strong) NSTextField        *hostField;
@property (strong) NSTextField        *userField;
@property (strong) NSTextField        *portField;
@property (strong) NSTextField        *keyfileField;
@property (strong) NSSecureTextField  *passwordField;   /* захищене поле */
@property (strong) NSTextView         *logView;
@property (strong) NSScrollView       *logScroll;
@property (strong) NSProgressIndicator *progress;
@property (strong) NSButton           *btnSend;
@property (strong) NSButton           *btnKeygen;

@end

@implementation AppDelegate

/* ------------------------------------------------------------------ */
/*  Вивід у лог-поле (викликається з будь-якого потоку)               */
/* ------------------------------------------------------------------ */
- (void)logAppend:(NSString *)msg
{
    dispatch_async(dispatch_get_main_queue(), ^{
        NSTextStorage *ts = self.logView.textStorage;
        NSDictionary *attrs = @{
            NSForegroundColorAttributeName :
                [NSColor colorWithCalibratedRed:0.20 green:1.00 blue:0.35 alpha:1.0],
            NSFontAttributeName :
                [NSFont fontWithName:@"Menlo" size:12] ?:
                [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightRegular]
        };
        NSAttributedString *as =
            [[NSAttributedString alloc] initWithString:msg attributes:attrs];
        [ts beginEditing];
        [ts appendAttributedString:as];
        [ts endEditing];
        /* Прокрутка вниз */
        NSRange end = NSMakeRange(ts.length, 0);
        [self.logView scrollRangeToVisible:end];
    });
}

/* ------------------------------------------------------------------ */
/*  Зчитати публічний ключ                                             */
/* ------------------------------------------------------------------ */
- (NSString *)readPubkey:(NSString *)path
{
    NSError *err = nil;
    NSString *raw = [NSString stringWithContentsOfFile:path
                                              encoding:NSUTF8StringEncoding
                                                 error:&err];
    if (!raw) {
        [self logAppend:[NSString stringWithFormat:
            @"[ERROR] Не вдалося прочитати ключ: %@\n         %@\n",
            path, err.localizedDescription]];
        return nil;
    }
    NSString *trimmed = [raw stringByTrimmingCharactersInSet:
                         [NSCharacterSet whitespaceAndNewlineCharacterSet]];
    if (trimmed.length == 0) {
        [self logAppend:@"[ERROR] Файл ключа порожній.\n"];
        return nil;
    }
    return trimmed;
}

/* ------------------------------------------------------------------ */
/*  Запуск зовнішньої команди, повертає exit-код                      */
/* ------------------------------------------------------------------ */
- (int)runCommand:(NSString *)cmd
{
    [self logAppend:[NSString stringWithFormat:@"[CMD] %@\n", cmd]];
    return system(cmd.UTF8String);
}

/* ------------------------------------------------------------------ */
/*  Кнопка "Огляд..."                                                  */
/* ------------------------------------------------------------------ */
- (void)browseKeyfile:(id)sender
{
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.title = @"Виберіть публічний SSH-ключ (.pub)";
    panel.canChooseFiles           = YES;
    panel.canChooseDirectories     = NO;
    panel.allowsMultipleSelection  = NO;
    panel.allowedFileTypes         = @[@"pub"];
    panel.allowsOtherFileTypes     = YES;

    [panel beginSheetModalForWindow:self.window
                  completionHandler:^(NSModalResponse r) {
        if (r == NSModalResponseOK)
            self.keyfileField.stringValue = panel.URL.path;
    }];
}

/* ------------------------------------------------------------------ */
/*  Кнопка "Генерувати ключ"                                           */
/* ------------------------------------------------------------------ */
- (void)generateKey:(id)sender
{
    NSString *home    = NSHomeDirectory();
    NSString *defpath = [home stringByAppendingPathComponent:@".ssh/id_rsa"];

    /* Запит шляху */
    NSAlert *inputAlert = [[NSAlert alloc] init];
    inputAlert.messageText = @"Генерація SSH-ключа";
    inputAlert.informativeText =
        [NSString stringWithFormat:
         @"Шлях до нового ключа (без .pub):\n(за замовчуванням: %@)", defpath];
    [inputAlert addButtonWithTitle:@"Генерувати"];
    [inputAlert addButtonWithTitle:@"Скасувати"];

    NSTextField *pathInput = [[NSTextField alloc]
                               initWithFrame:NSMakeRect(0, 0, 340, 24)];
    pathInput.stringValue = defpath;
    inputAlert.accessoryView = pathInput;

    if ([inputAlert runModal] != NSAlertFirstButtonReturn) return;

    NSString *keypath = pathInput.stringValue;
    if (keypath.length == 0) keypath = defpath;

    /* Запит коментаря */
    NSAlert *commentAlert = [[NSAlert alloc] init];
    commentAlert.messageText = @"Коментар до ключа";
    commentAlert.informativeText = @"(зазвичай email або ім'я, можна залишити порожнім)";
    [commentAlert addButtonWithTitle:@"OK"];
    [commentAlert addButtonWithTitle:@"Пропустити"];
    NSTextField *commentInput = [[NSTextField alloc]
                                  initWithFrame:NSMakeRect(0, 0, 340, 24)];
    commentInput.placeholderString = @"user@hostname";
    commentAlert.accessoryView = commentInput;
    [commentAlert runModal];
    NSString *comment = commentInput.stringValue;

    /* Блокуємо кнопки */
    self.btnSend.enabled   = NO;
    self.btnKeygen.enabled = NO;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NSString *cmd;
        if (comment.length > 0) {
            cmd = [NSString stringWithFormat:
                   @"ssh-keygen -t rsa -b 4096 -C '%@' -f '%@'",
                   comment, keypath];
        } else {
            cmd = [NSString stringWithFormat:
                   @"ssh-keygen -t rsa -b 4096 -f '%@'", keypath];
        }

        [self logAppend:@"\n--- Генерація ключа ---\n"];
        int ret = [self runCommand:cmd];

        NSString *pubpath = [keypath stringByAppendingString:@".pub"];

        dispatch_async(dispatch_get_main_queue(), ^{
            self.btnSend.enabled   = YES;
            self.btnKeygen.enabled = YES;

            if (ret == 0) {
                self.keyfileField.stringValue = pubpath;
                [self logAppend:[NSString stringWithFormat:
                    @"[OK] Ключ згенеровано: %@\n"
                     "     Шлях оновлено автоматично.\n", pubpath]];
            } else {
                [self logAppend:[NSString stringWithFormat:
                    @"[ERROR] ssh-keygen завершився з кодом %d\n", ret]];
            }
        });
    });
}

/* ------------------------------------------------------------------ */
/*  Кнопка "Передати ключ"                                             */
/* ------------------------------------------------------------------ */
- (void)sendKey:(id)sender
{
    NSString *host     = [self.hostField.stringValue
                          stringByTrimmingCharactersInSet:
                          NSCharacterSet.whitespaceCharacterSet];
    NSString *user     = [self.userField.stringValue
                          stringByTrimmingCharactersInSet:
                          NSCharacterSet.whitespaceCharacterSet];
    NSString *portStr  = self.portField.stringValue;
    NSString *keyfile  = [self.keyfileField.stringValue
                          stringByTrimmingCharactersInSet:
                          NSCharacterSet.whitespaceCharacterSet];
    NSString *password = self.passwordField.stringValue;

    /* ---- Валідація ---- */
    NSMutableArray *missing = [NSMutableArray array];
    if (host.length    == 0) [missing addObject:@"• Хост"];
    if (user.length    == 0) [missing addObject:@"• Користувач"];
    if (keyfile.length == 0) [missing addObject:@"• Шлях до ключа"];

    if (missing.count > 0) {
        NSAlert *a = [[NSAlert alloc] init];
        a.messageText     = @"Заповніть обов'язкові поля";
        a.informativeText = [missing componentsJoinedByString:@"\n"];
        a.alertStyle      = NSAlertStyleWarning;
        [a addButtonWithTitle:@"OK"];
        [a runModal];
        return;
    }

    int port = portStr.intValue;
    if (port <= 0 || port > 65535) port = 22;

    /* ---- Зчитати ключ ---- */
    NSString *pubkey = [self readPubkey:keyfile];
    if (!pubkey) return;

    /* ---- Блокуємо кнопки, запускаємо прогрес ---- */
    self.btnSend.enabled   = NO;
    self.btnKeygen.enabled = NO;
    [self.progress setIndeterminate:YES];
    [self.progress startAnimation:nil];
    [self logAppend:@"\n--- Розпочинаємо передачу ключа ---\n"];

    /* ---- Команда на сервері ---- */
    NSString *remoteCmd = [NSString stringWithFormat:
        @"mkdir -p ~/.ssh && chmod 700 ~/.ssh && "
        @"grep -qxF '%@' ~/.ssh/authorized_keys 2>/dev/null || "
        @"echo '%@' >> ~/.ssh/authorized_keys && "
        @"chmod 600 ~/.ssh/authorized_keys && echo KEY_OK",
        pubkey, pubkey];

    /* Знімаємо сильні посилання для потоку */
    NSString *captHost     = host;
    NSString *captUser     = user;
    NSString *captPassword = password;
    int       captPort     = port;

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        int ret;

        if (captPassword.length > 0) {
            /* Спочатку пробуємо sshpass */
            NSString *checkSshpass =
                @"command -v sshpass >/dev/null 2>&1";
            BOOL hasSshpass = (system(checkSshpass.UTF8String) == 0);

            if (hasSshpass) {
                /* Пароль передаємо через змінну середовища SSHPASS
                   щоб він не потрапив у ps aux */
                NSString *sshCmd = [NSString stringWithFormat:
                    @"SSHPASS='%@' sshpass -e ssh "
                    @"-o StrictHostKeyChecking=accept-new "
                    @"-p %d '%@@%@' '%@'",
                    captPassword, captPort, captUser, captHost, remoteCmd];
                [self logAppend:[NSString stringWithFormat:
                    @"[INFO] sshpass: підключення до %@@%@:%d\n",
                    captUser, captHost, captPort]];
                ret = system(sshCmd.UTF8String);
            } else {
                [self logAppend:
                    @"[WARN] sshpass не знайдено. Пробуємо ssh без пароля.\n"
                    @"[HINT] Встановити: brew install hudochenkov/sshpass/sshpass\n"];
                NSString *sshCmd = [NSString stringWithFormat:
                    @"ssh -o StrictHostKeyChecking=accept-new "
                    @"-p %d '%@@%@' '%@'",
                    captPort, captUser, captHost, remoteCmd];
                [self logAppend:[NSString stringWithFormat:
                    @"[INFO] ssh: підключення до %@@%@:%d\n",
                    captUser, captHost, captPort]];
                ret = system(sshCmd.UTF8String);
            }
        } else {
            /* Без пароля — звичайний SSH (ключ / агент) */
            NSString *sshCmd = [NSString stringWithFormat:
                @"ssh -o StrictHostKeyChecking=accept-new "
                @"-p %d '%@@%@' '%@'",
                captPort, captUser, captHost, remoteCmd];
            [self logAppend:[NSString stringWithFormat:
                @"[INFO] ssh: підключення до %@@%@:%d\n",
                captUser, captHost, captPort]];
            ret = system(sshCmd.UTF8String);
        }

        /* ---- Результат — повертаємося в головний потік ---- */
        int finalRet = ret;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.progress stopAnimation:nil];
            [self.progress setIndeterminate:NO];
            self.btnSend.enabled   = YES;
            self.btnKeygen.enabled = YES;

            if (finalRet == 0) {
                NSString *okMsg = [NSString stringWithFormat:
                    @"\n[OK] Ключ передано на %@@%@\n"
                     "[OK] Підключення: ssh -p %d %@@%@\n\n",
                    captUser, captHost, captPort, captUser, captHost];
                [self logAppend:okMsg];

                NSAlert *a = [[NSAlert alloc] init];
                a.messageText     = @"✅ Ключ успішно передано!";
                a.informativeText = okMsg;
                a.alertStyle      = NSAlertStyleInformational;
                [a addButtonWithTitle:@"OK"];
                [a runModal];

            } else {
                NSString *errMsg = [NSString stringWithFormat:
                    @"[ERROR] SSH завершився з кодом %d\n", finalRet];
                [self logAppend:errMsg];

                NSAlert *a = [[NSAlert alloc] init];
                a.messageText     = @"❌ Помилка передачі";
                a.informativeText =
                    @"Дивіться деталі в лозі.\n\n"
                    @"Можливі причини:\n"
                    @"• Неправильний хост або порт\n"
                    @"• Неправильний пароль\n"
                    @"• SSH-сервер відхилив з'єднання";
                a.alertStyle      = NSAlertStyleCritical;
                [a addButtonWithTitle:@"OK"];
                [a runModal];
            }
        });
    });
}

/* ------------------------------------------------------------------ */
/*  Завантаження іконки (ssh-copy-id.png поряд із бінарником)         */
/* ------------------------------------------------------------------ */
- (NSImage *)loadAppIcon
{
    /* Шлях до поточного бінарника */
    NSString *binDir = [[[NSProcessInfo processInfo].arguments firstObject]
                        stringByDeletingLastPathComponent];
    NSArray *candidates = @[
        [binDir stringByAppendingPathComponent:@"ssh-copy-id.png"],
        [[NSBundle mainBundle] pathForResource:@"ssh-copy-id" ofType:@"png"] ?: @""
    ];
    for (NSString *p in candidates) {
        if (p.length > 0 &&
            [[NSFileManager defaultManager] fileExistsAtPath:p]) {
            NSImage *img = [[NSImage alloc] initWithContentsOfFile:p];
            if (img) return img;
        }
    }
    return nil;
}

/* ------------------------------------------------------------------ */
/*  Побудова UI                                                        */
/* ------------------------------------------------------------------ */
- (void)buildUI
{
    /* ---- Вікно ---- */
    NSRect winFrame = NSMakeRect(0, 0, 640, 580);
    self.window = [[NSWindow alloc]
        initWithContentRect:winFrame
                  styleMask:(NSWindowStyleMaskTitled         |
                             NSWindowStyleMaskClosable       |
                             NSWindowStyleMaskMiniaturizable |
                             NSWindowStyleMaskResizable)
                    backing:NSBackingStoreBuffered
                      defer:NO];
    self.window.title    = @"SSH Key Copy — macOS → Unix/Linux";
    self.window.minSize  = NSMakeSize(580, 540);

    /* Іконка в Dock */
    NSImage *icon = [self loadAppIcon];
    if (icon) [NSApp setApplicationIconImage:icon];

    NSView *cv = self.window.contentView;

    /* Колір фону вікна */
    self.window.backgroundColor =
        [NSColor colorWithCalibratedRed:0.94 green:0.96 blue:0.99 alpha:1.0];

    /* ---- Розміри сітки ---- */
    const CGFloat LX   = 14;    /* x лейбла */
    const CGFloat LW   = 178;   /* ширина лейбла */
    const CGFloat FX   = 198;   /* x поля вводу */
    const CGFloat FW   = 418;   /* ширина поля вводу */
    const CGFloat FH   = 24;    /* висота поля */
    const CGFloat STEP = 36;    /* крок між рядками */
    /* y відраховуємо знизу (Cocoa — знизу вгору) */
    CGFloat y = 400;

    /* ---- Допоміжний блок для рядка лейбл+поле ---- */
    __block CGFloat rowY = y;

    /* Лямбда-блок для стандартного рядка */
    NSTextField * __strong *(^makeRow)(NSString *) =
        ^NSTextField * __strong *(NSString *label) {

        NSTextField *lbl = [NSTextField labelWithString:label];
        lbl.frame      = NSMakeRect(LX, rowY + 2, LW, FH);
        lbl.alignment  = NSTextAlignmentRight;
        lbl.font       = [NSFont systemFontOfSize:13 weight:NSFontWeightMedium];
        lbl.textColor  = [NSColor labelColor];
        [cv addSubview:lbl];

        NSTextField *field = [[NSTextField alloc]
                               initWithFrame:NSMakeRect(FX, rowY, FW, FH)];
        field.font        = [NSFont systemFontOfSize:13];
        field.bezeled     = YES;
        field.bezelStyle  = NSTextFieldRoundedBezel;
        field.editable    = YES;
        field.selectable  = YES;
        [cv addSubview:field];

        rowY -= STEP;
        return &field; /* Повертаємо адресу локальної змінної — НЕ можна */
        /* Тому використаємо аутлети напряму — дивись нижче */
    };
    (void)makeRow; /* Заглушка — не використовуємо цей варіант */

    /* ---- Іконка у вікні (зліва від заголовка форми) ---- */
    if (icon) {
        NSImageView *iv = [NSImageView imageViewWithImage:icon];
        iv.frame = NSMakeRect(LX, rowY + STEP * 5 + 4, 48, 48);
        [cv addSubview:iv];
    }

    /* ---- Заголовок ---- */
    NSTextField *title = [NSTextField labelWithString:@"SSH Key Copy"];
    title.frame      = NSMakeRect(LX + 58, rowY + STEP * 5 + 24, 500, 28);
    title.font       = [NSFont boldSystemFontOfSize:18];
    title.textColor  = [NSColor labelColor];
    [cv addSubview:title];

    NSTextField *subtitle = [NSTextField
        labelWithString:@"Передача публічного SSH-ключа на Unix/Linux сервер"];
    subtitle.frame     = NSMakeRect(LX + 58, rowY + STEP * 5 + 4, 500, 18);
    subtitle.font      = [NSFont systemFontOfSize:11];
    subtitle.textColor = [NSColor secondaryLabelColor];
    [cv addSubview:subtitle];

    /* ---- Рядок: Хост ---- */
    {
        NSTextField *lbl = [NSTextField labelWithString:@"Хост (IP / hostname):"];
        lbl.frame = NSMakeRect(LX, rowY+2, LW, FH);
        lbl.alignment = NSTextAlignmentRight;
        lbl.font = [NSFont systemFontOfSize:13];
        [cv addSubview:lbl];

        self.hostField = [[NSTextField alloc] initWithFrame:NSMakeRect(FX, rowY, FW, FH)];
        self.hostField.font = [NSFont systemFontOfSize:13];
        self.hostField.bezeled = YES;
        self.hostField.bezelStyle = NSTextFieldRoundedBezel;
        self.hostField.editable = YES;
        self.hostField.placeholderString = @"наприклад: 192.168.1.10 або server.example.com";
        [cv addSubview:self.hostField];
        rowY -= STEP;
    }

    /* ---- Рядок: Користувач ---- */
    {
        NSTextField *lbl = [NSTextField labelWithString:@"Користувач:"];
        lbl.frame = NSMakeRect(LX, rowY+2, LW, FH);
        lbl.alignment = NSTextAlignmentRight;
        lbl.font = [NSFont systemFontOfSize:13];
        [cv addSubview:lbl];

        self.userField = [[NSTextField alloc] initWithFrame:NSMakeRect(FX, rowY, FW, FH)];
        self.userField.font = [NSFont systemFontOfSize:13];
        self.userField.bezeled = YES;
        self.userField.bezelStyle = NSTextFieldRoundedBezel;
        self.userField.editable = YES;
        self.userField.placeholderString = @"наприклад: admin";
        [cv addSubview:self.userField];
        rowY -= STEP;
    }

    /* ---- Рядок: Порт ---- */
    {
        NSTextField *lbl = [NSTextField labelWithString:@"SSH порт:"];
        lbl.frame = NSMakeRect(LX, rowY+2, LW, FH);
        lbl.alignment = NSTextAlignmentRight;
        lbl.font = [NSFont systemFontOfSize:13];
        [cv addSubview:lbl];

        self.portField = [[NSTextField alloc] initWithFrame:NSMakeRect(FX, rowY, 80, FH)];
        self.portField.font = [NSFont systemFontOfSize:13];
        self.portField.bezeled = YES;
        self.portField.bezelStyle = NSTextFieldRoundedBezel;
        self.portField.editable = YES;
        self.portField.stringValue = @"22";
        self.portField.placeholderString = @"22";
        /* Обмежити введення до цифр */
        NSNumberFormatter *fmt = [[NSNumberFormatter alloc] init];
        fmt.numberStyle = NSNumberFormatterNoStyle;
        self.portField.formatter = fmt;
        [cv addSubview:self.portField];
        rowY -= STEP;
    }

    /* ---- Рядок: Ключ + Огляд ---- */
    {
        NSTextField *lbl = [NSTextField labelWithString:@"Шлях до .pub ключа:"];
        lbl.frame = NSMakeRect(LX, rowY+2, LW, FH);
        lbl.alignment = NSTextAlignmentRight;
        lbl.font = [NSFont systemFontOfSize:13];
        [cv addSubview:lbl];

        /* Поле ключа займає FW - 90 (місце для кнопки "Огляд") */
        self.keyfileField = [[NSTextField alloc]
                              initWithFrame:NSMakeRect(FX, rowY, FW - 90, FH)];
        self.keyfileField.font = [NSFont systemFontOfSize:13];
        self.keyfileField.bezeled = YES;
        self.keyfileField.bezelStyle = NSTextFieldRoundedBezel;
        self.keyfileField.editable = YES;
        self.keyfileField.stringValue =
            [NSHomeDirectory() stringByAppendingPathComponent:@".ssh/id_rsa.pub"];
        self.keyfileField.placeholderString = @"~/.ssh/id_rsa.pub";
        [cv addSubview:self.keyfileField];

        /* Кнопка "Огляд..." */
        NSButton *btnBrowse = [NSButton buttonWithTitle:@"📂 Огляд…"
                                                 target:self
                                                 action:@selector(browseKeyfile:)];
        btnBrowse.frame = NSMakeRect(FX + FW - 84, rowY - 1, 84, 28);
        [cv addSubview:btnBrowse];
        rowY -= STEP;
    }

    /* ---- Рядок: Пароль ---- */
    {
        NSTextField *lbl = [NSTextField labelWithString:@"Пароль (SSH):"];
        lbl.frame = NSMakeRect(LX, rowY+2, LW, FH);
        lbl.alignment = NSTextAlignmentRight;
        lbl.font = [NSFont systemFontOfSize:13];
        [cv addSubview:lbl];

        /* NSSecureTextField — символи замінені на ••• */
        self.passwordField = [[NSSecureTextField alloc]
                               initWithFrame:NSMakeRect(FX, rowY, FW, FH)];
        self.passwordField.font = [NSFont systemFontOfSize:13];
        self.passwordField.bezeled = YES;
        self.passwordField.bezelStyle = NSTextFieldRoundedBezel;
        self.passwordField.editable = YES;
        self.passwordField.placeholderString =
            @"залиште порожнім якщо є SSH-ключ або ssh-agent";
        [cv addSubview:self.passwordField];
        rowY -= STEP;
    }

    /* ---- Примітка про sshpass ---- */
    {
        NSTextField *note = [NSTextField labelWithString:
            @"⚠️  Для автоматичної передачі пароля: brew install hudochenkov/sshpass/sshpass"];
        note.frame     = NSMakeRect(FX, rowY + 8, FW, 16);
        note.font      = [NSFont systemFontOfSize:10];
        note.textColor = [NSColor systemOrangeColor];
        [cv addSubview:note];
        rowY -= 26;
    }

    /* ---- Роздільник ---- */
    NSBox *sep = [[NSBox alloc] initWithFrame:NSMakeRect(LX, rowY, LX + LW + FW, 1)];
    sep.boxType = NSBoxSeparator;
    [cv addSubview:sep];
    rowY -= 12;

    /* ---- Кнопки дій ---- */
    {
        /* 🔑 Генерувати ключ */
        self.btnKeygen = [NSButton buttonWithTitle:@"🔑  Генерувати ключ"
                                            target:self
                                            action:@selector(generateKey:)];
        self.btnKeygen.frame = NSMakeRect(FX, rowY - 4, 180, 30);
        [cv addSubview:self.btnKeygen];

        /* 🚀 Передати ключ — акцентна кнопка */
        self.btnSend = [NSButton buttonWithTitle:@"🚀  Передати ключ"
                                          target:self
                                          action:@selector(sendKey:)];
        self.btnSend.frame         = NSMakeRect(FX + 190, rowY - 4, 178, 30);
        self.btnSend.bezelStyle    = NSBezelStyleRounded;
        self.btnSend.keyEquivalent = @"\r";
        [cv addSubview:self.btnSend];

        rowY -= 42;
    }

    /* ---- Прогрес-смуга ---- */
    {
        self.progress = [[NSProgressIndicator alloc]
                          initWithFrame:NSMakeRect(LX, rowY, LX + LW + FW, 10)];
        self.progress.style = NSProgressIndicatorStyleBar;
        self.progress.indeterminate = YES;
        self.progress.displayedWhenStopped = NO;
        [cv addSubview:self.progress];
        rowY -= 16;
    }

    /* ---- Лог-панель (ScrollView + TextView) ---- */
    {
        CGFloat logH = rowY - 8;   /* займає весь простір, що залишився */
        self.logScroll = [[NSScrollView alloc]
                           initWithFrame:NSMakeRect(LX, 8, LX + LW + FW, logH)];
        self.logScroll.hasVerticalScroller  = YES;
        self.logScroll.autohidesScrollers   = YES;
        self.logScroll.borderType           = NSBezelBorder;

        self.logView = [[NSTextView alloc] initWithFrame:self.logScroll.bounds];
        self.logView.editable          = NO;
        self.logView.richText          = YES;
        self.logView.backgroundColor   =
            [NSColor colorWithCalibratedRed:0.05 green:0.05 blue:0.10 alpha:1.0];
        self.logView.insertionPointColor = NSColor.greenColor;
        self.logView.autoresizingMask  = NSViewWidthSizable | NSViewHeightSizable;

        self.logScroll.documentView    = self.logView;
        [cv addSubview:self.logScroll];
    }

    [self.window center];
    [self.window makeKeyAndOrderFront:nil];
}

/* ------------------------------------------------------------------ */
/*  applicationDidFinishLaunching                                      */
/* ------------------------------------------------------------------ */
- (void)applicationDidFinishLaunching:(NSNotification *)notif
{
    [self buildUI];
    [self logAppend:@"SSH Key Copy готовий до роботи.\n"];
    [self logAppend:@"Заповніть поля вгорі та натисніть «🚀 Передати ключ».\n\n"];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app
{
    return YES;
}

@end

/* ================================================================== */
/*  main                                                               */
/* ================================================================== */
int main(int argc, const char *argv[])
{
    setlocale(LC_ALL, "");
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        app.activationPolicy = NSApplicationActivationPolicyRegular;

        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;

        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
