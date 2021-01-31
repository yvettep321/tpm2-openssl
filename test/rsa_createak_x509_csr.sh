#!/bin/bash
set -eufx

# create EK
tpm2_createek -G rsa -c ek_rsa.ctx

# create AK with defined scheme/hash
tpm2_createak -C ek_rsa.ctx -G rsa -g sha256 -s rsassa -c ak_rsa.ctx

# load the AK to persistent handle
HANDLE=$(tpm2_evictcontrol --object-context=ak_rsa.ctx | cut -d ' ' -f 2 | head -n 1)

# create a private key and then generate a certificate request from it
openssl req -provider tpm2 -new -subj "/C=GB/CN=foo" -key handle:${HANDLE} -out testcsr.pem

# display private key info
openssl rsa -provider tpm2 -in handle:${HANDLE} -text -noout

# display content of the created request
openssl req -text -noout -verify -in testcsr.pem

# release persistent handle
tpm2_evictcontrol --object-context=${HANDLE}

rm ek_rsa.ctx ak_rsa.ctx testcsr.pem