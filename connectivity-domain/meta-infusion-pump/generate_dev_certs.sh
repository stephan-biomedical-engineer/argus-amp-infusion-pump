#!/bin/bash
# NUNCA USE ESTES CERTIFICADOS EM PRODUÇÃO!

# Garante que estamos na pasta da layer (meta-infusion-pump/)
cd "$(dirname "$0")"
ROOT_DIR=$(pwd)

echo "--- Iniciando Geração de Certificados de DEV ---"

# 1. RAUC (Assinatura de Bundles)
mkdir -p $ROOT_DIR/recipes-core/bundles/files/
mkdir -p $ROOT_DIR/recipes-support/rauc/files/
openssl req -x509 -newkey rsa:4096 -nodes \
    -keyout $ROOT_DIR/recipes-core/bundles/files/development-1.key.pem \
    -out $ROOT_DIR/recipes-core/bundles/files/development-1.cert.pem \
    -subj "/CN=Argus-RAUC-Dev" -days 365
cp $ROOT_DIR/recipes-core/bundles/files/development-1.cert.pem $ROOT_DIR/recipes-support/rauc/files/keyring.pem

# 2. MOSQUITTO (mTLS)
CERT_DEST=$ROOT_DIR/recipes-connectivity/mosquitto/files/certs/
mkdir -p $CERT_DEST

# CA Raiz
openssl req -new -x509 -days 3650 -extensions v3_ca \
    -keyout ca.key -out ca.crt \
    -subj "/CN=Argus-Root-CA/O=Argus-Medical" -nodes

# Servidor (Bomba)
openssl genrsa -out server.key 2048
openssl req -new -out server.csr -key server.key -subj "/CN=argus-pump.local" -nodes
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out server.crt -days 365

# Cliente (Backend)
openssl genrsa -out backend.key 2048
openssl req -new -out backend.csr -key backend.key -subj "/CN=backend-service" -nodes
openssl x509 -req -in backend.csr -CA ca.crt -CAkey ca.key -CAcreateserial -out backend.crt -days 365

# Organização
#
# A chave da CA NAO pode ficar dentro da layer: o .gitignore so cobre
# recipes-connectivity/mosquitto/files/certs/, e uma ca.key solta na raiz da
# layer acaba versionada -- foi assim que ela vazou uma vez. Ela vai para
# backend_certs/, junto com as credenciais de cliente do backend.
mkdir -p ${ROOT_DIR}/../backend_certs/
mv ca.crt server.crt server.key $CERT_DEST/
mv ca.key backend.crt backend.key ./*.csr ./*.srl ${ROOT_DIR}/../backend_certs/
chmod 600 ${ROOT_DIR}/../backend_certs/*.key

echo "--- Sucesso! ---"
echo "Certificados da Bomba: $CERT_DEST"
echo "Certificados do seu Backend (PC): ${ROOT_DIR}/../backend_certs/"
