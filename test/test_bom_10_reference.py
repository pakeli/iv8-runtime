"""Offline integrity and independent vector checks for BOM-10 browser evidence.

These validate the reference artifacts, not implementation of the future APIs.
"""

from datetime import datetime
import hashlib
import json
from pathlib import Path

import pytest


FIXTURES = Path(__file__).resolve().parent / "fixtures" / "bom_10"
CONTEXTS = ("secure", "insecure", "opaque")


def load(group, context="secure"):
    return json.loads((FIXTURES / f"{group}-{context}.json").read_text())


def check_complete(value):
    if isinstance(value, dict):
        assert value.get("status") != "probe-failed"
        assert value.get("outcome") != "timeout"
        for child in value.values():
            check_complete(child)
    elif isinstance(value, list):
        for child in value:
            check_complete(child)


@pytest.mark.parametrize("group", ("crypto", "storage", "digest_edges", "storage_tree"))
@pytest.mark.parametrize("context", CONTEXTS)
def test_bom10_reference_receipt_is_complete(group, context):
    fixture = load(group, context)
    assert fixture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / f"{group}_probe.js").read_bytes()
    ).hexdigest()
    assert fixture["browser"] == "Google Chrome 158.0.8127.88"
    assert datetime.fromisoformat(fixture["captured_at_utc"]).tzinfo is not None
    assert fixture["result"]["secure"] is (context == "secure")
    assert "private local DevTools pipe" in fixture["execution"]["transport"]
    check_complete(fixture["result"])


@pytest.mark.parametrize("algorithm", ("SHA-1", "SHA-256", "SHA-384", "SHA-512"))
@pytest.mark.parametrize("case,data", (("abc", b"abc"), ("empty", b"")))
def test_bom10_chrome_digest_matches_independent_hashlib_vector(algorithm, case, data):
    result = load("crypto")["result"]["digest"][algorithm][case]
    expected = hashlib.new(algorithm.lower().replace("-", ""), data)
    assert result["mode"] == "promise"
    assert result["outcome"] == "fulfilled"
    assert result["value"] == {
        "type": "ArrayBuffer", "byteLength": expected.digest_size,
        "hex": expected.hexdigest(),
    }


def test_bom10_digest_view_snapshot_and_exception_reference():
    digest = load("crypto")["result"]["digest"]
    for case in ("typedArrayOffset", "dataViewOffset", "arrayBuffer",
                 "snapshotBeforeMutation", "lowercaseAlgorithm", "extraArgument"):
        assert digest[case] == digest["SHA-256"]["abc"]
    assert digest["unknownAlgorithm"]["error"]["name"] == "NotSupportedError"
    assert digest["throwingAlgorithmGetter"]["sameError"] is True
    assert digest["invalidReceiverWithValidArgs"]["outcome"] == "rejected"


@pytest.mark.parametrize("context", CONTEXTS)
def test_bom10_secure_context_exposure_reference(context):
    crypto = load("crypto", context)["result"]
    storage = load("storage", context)["result"]
    secure = context == "secure"
    assert crypto["instances"]["crypto"]["subtlePresent"] is secure
    assert crypto["globals"]["SubtleCrypto"]["present"] is secure
    assert crypto["globals"]["CryptoKey"]["present"] is secure
    assert storage["storagePresent"] is secure


def test_bom10_opfs_identity_and_io_reference():
    behavior = load("storage")["result"]["behavior"]
    assert behavior["rootIdentity"] == {"sameWrapper": False, "sameEntry": True}
    assert behavior["root"]["value"]["kind"] == "directory"
    assert behavior["root"]["value"]["name"] == ""
    assert behavior["resolveFile"] == ["bom10-reference", "note.txt"]
    assert behavior["resolveSelf"] == []
    assert behavior["resolveOutside"] is None
    assert behavior["writtenText"] == "hello"
    assert behavior["wrongKind"]["error"]["name"] == "TypeMismatchError"
    assert behavior["removeNonempty"]["error"]["name"] == "InvalidModificationError"
    assert behavior["afterRemove"]["error"]["name"] == "NotFoundError"
