#!/bin/bash
# =============================================================
# sign_selfsigned.sh — самопідписаний сертифікат + підпис exe
#
# Використання (Linux/WSL з osslsigncode та openssl):
#   chmod +x sign_selfsigned.sh
#   ./sign_selfsigned.sh ssh_key_copy.exe
#
# Що робить:
#   1. Генерує кореневий CA сертифікат (ROOT CA)
#   2. Генерує Code Signing сертифікат підписаний ROOT CA
#   3. Підписує .exe через osslsigncode
#   4. Виводить інструкцію як встановити ROOT CA на цільових ПК
#
# Встановити osslsigncode:
#   sudo apt install osslsigncode   # Debian/Ubuntu
#   brew install osslsigncode        # macOS
# =============================================================

set -e

EXE="${1:-ssh_key_copy.exe}"
COMPANY="MyOrganization"
COMMON_NAME="SSH Key Copy"
COUNTRY="UA"
DAYS_CA=3650    # 10 років для CA
DAYS_CERT=1095  # 3 роки для code signing cert

# Кольори
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

echo -e "${GREEN}=== SSH Key Copy — Code Signing ===${NC}"
echo

# ── Перевірка залежностей ──────────────────────────────────
for tool in openssl osslsigncode; do
    if ! command -v $tool &>/dev/null; then
        echo -e "${RED}[ERROR] $tool не знайдено.${NC}"
        echo "  sudo apt install osslsigncode openssl"
        exit 1
    fi
done

if [ ! -f "$EXE" ]; then
    echo -e "${RED}[ERROR] Файл не знайдено: $EXE${NC}"
    exit 1
fi

mkdir -p codesign
cd codesign

# ── 1. Генеруємо ROOT CA ───────────────────────────────────
echo -e "${YELLOW}[1/4] Генерація кореневого CA сертифікату...${NC}"

if [ ! -f root_ca.key ]; then
    openssl genrsa -out root_ca.key 4096
fi

cat > root_ca.conf << EOF
[req]
distinguished_name = dn
x509_extensions    = v3_ca
prompt             = no

[dn]
C  = ${COUNTRY}
O  = ${COMPANY}
CN = ${COMPANY} Root CA

[v3_ca]
subjectKeyIdentifier   = hash
authorityKeyIdentifier = keyid:always,issuer
basicConstraints       = critical,CA:TRUE
keyUsage               = critical,digitalSignature,cRLSign,keyCertSign
EOF

openssl req -new -x509 \
    -key root_ca.key \
    -out root_ca.crt \
    -days ${DAYS_CA} \
    -config root_ca.conf

echo -e "${GREEN}  root_ca.crt створено${NC}"

# ── 2. Генеруємо Code Signing сертифікат ──────────────────
echo -e "${YELLOW}[2/4] Генерація Code Signing сертифікату...${NC}"

openssl genrsa -out codesign.key 4096

cat > codesign.conf << EOF
[req]
distinguished_name = dn
req_extensions     = v3_req
prompt             = no

[dn]
C  = ${COUNTRY}
O  = ${COMPANY}
CN = ${COMMON_NAME}

[v3_req]
basicConstraints     = CA:FALSE
keyUsage             = critical,digitalSignature
extendedKeyUsage     = critical,codeSigning
subjectKeyIdentifier = hash
EOF

openssl req -new \
    -key codesign.key \
    -out codesign.csr \
    -config codesign.conf

cat > codesign_ext.conf << EOF
basicConstraints     = CA:FALSE
keyUsage             = critical,digitalSignature
extendedKeyUsage     = critical,codeSigning
subjectKeyIdentifier = hash
authorityKeyIdentifier = keyid,issuer
EOF

openssl x509 -req \
    -in codesign.csr \
    -CA root_ca.crt \
    -CAkey root_ca.key \
    -CAcreateserial \
    -out codesign.crt \
    -days ${DAYS_CERT} \
    -extfile codesign_ext.conf

# Пакуємо у PKCS#12 (.pfx)
PFXPASS="changeme123"
openssl pkcs12 -export \
    -out codesign.pfx \
    -inkey codesign.key \
    -in codesign.crt \
    -certfile root_ca.crt \
    -passout pass:${PFXPASS}

echo -e "${GREEN}  codesign.pfx створено (пароль: ${PFXPASS})${NC}"

# ── 3. Підписуємо .exe ─────────────────────────────────────
echo -e "${YELLOW}[3/4] Підписування ${EXE}...${NC}"

TMP_SIGNED="../${EXE%.exe}_tmp_signed.exe"
FINAL="../${EXE}"   # перезаписуємо оригінал підписаною версією

# osslsigncode не може читати і писати в один файл — використовуємо tmp
osslsigncode sign \
    -pkcs12 codesign.pfx \
    -pass "${PFXPASS}" \
    -n "${COMMON_NAME}" \
    -i "https://github.com/your-org/ssh-key-copy" \
    -t "http://timestamp.digicert.com" \
    -in "../${EXE}" \
    -out "${TMP_SIGNED}" 2>/dev/null || \
osslsigncode sign \
    -pkcs12 codesign.pfx \
    -pass "${PFXPASS}" \
    -n "${COMMON_NAME}" \
    -in "../${EXE}" \
    -out "${TMP_SIGNED}"

# Замінюємо оригінал підписаним
mv -f "${TMP_SIGNED}" "${FINAL}"
echo -e "${GREEN}  Підписано: ${FINAL}${NC}"

# ── 4. Перевірка підпису ───────────────────────────────────
echo -e "${YELLOW}[4/4] Перевірка підпису...${NC}"
osslsigncode verify -CAfile root_ca.crt "${FINAL}" && \
    echo -e "${GREEN}  Підпис валідний!${NC}" || \
    echo -e "${RED}  Помилка перевірки${NC}"

cd ..

echo
echo -e "${GREEN}=== Готово ===${NC}"
echo
echo -e "${YELLOW}Файли:${NC}"
echo "  ${SIGNED}           — підписаний exe"
echo "  codesign/root_ca.crt — кореневий CA (встановити на цільових ПК)"
echo
echo -e "${YELLOW}Встановлення ROOT CA на Windows ПК (одноразово):${NC}"
echo "  1. Скопіюйте root_ca.crt на цільовий ПК"
echo "  2. Подвійний клік → Встановити сертифікат"
echo "  3. Розміщення: Локальний комп'ютер"
echo "  4. Сховище: Довірені кореневі центри сертифікації"
echo "  5. Готово — програма більше не блокуватиметься SmartScreen"
echo
echo -e "${YELLOW}Або через PowerShell (адміністратор):${NC}"
echo "  Import-Certificate -FilePath root_ca.crt \\"
echo "    -CertStoreLocation Cert:\LocalMachine\Root"
echo
echo -e "${YELLOW}Або через Group Policy (для всіх ПК організації):${NC}"
echo "  Конфігурація комп'ютера → Параметри Windows → Параметри безпеки"
echo "  → Політики відкритого ключа → Довірені кореневі центри сертифікації"
