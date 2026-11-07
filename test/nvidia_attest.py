#!/usr/bin/env python3
"""Appraise the supplied NVML evidence using NVIDIA SDK 2.7.3/NRAS.
No no-GPU, developer-mode, stale-token, or unsigned-JWT fallback is accepted.
"""
import hashlib,json,sys
from nv_attestation_sdk.gpu import attest_gpu_remote
from nv_attestation_sdk.utils import nras_utils, unified_eat_parser
from nv_attestation_sdk.utils.config import REMOTE_GPU_VERIFIER_SERVICE_URL
binding=bytes.fromhex(sys.argv[1])
evidence=json.loads(sys.stdin.buffer.read(65536))
nonce=hashlib.sha256(binding).hexdigest()
if evidence.get('nonce')!=nonce or evidence.get('arch') not in ('HOPPER','BLACKWELL'):
 raise SystemExit('evidence challenge/architecture mismatch')
result,token=attest_gpu_remote.attest(nonce,[evidence],{'claims_version':'2.0'})
if result is not True:raise SystemExit('NVIDIA attestation failed')
# The SDK verifies the NRAS JWT using its authenticated key discovery.
claims=nras_utils.decode_nras_token(REMOTE_GPU_VERIFIER_SERVICE_URL,unified_eat_parser.get_overall_claims_token(token))
if claims.get('eat_nonce')!=nonce:raise SystemExit('signed NRAS nonce mismatch')
print(json.dumps({'attested':True,'nonce':nonce,'claims':claims}))
