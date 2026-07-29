import json
import os

from app.content import load_json_object


def test_load_json_object_reads_dict(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"schema": 1, "version": "1.4.0"}), encoding="utf-8")
    cache = {}
    assert load_json_object(str(p), cache) == {"schema": 1, "version": "1.4.0"}


def test_load_json_object_missing_file(tmp_path):
    assert load_json_object(str(tmp_path / "nope.json"), {}) == {}


def test_load_json_object_broken_json(tmp_path):
    p = tmp_path / "update.json"
    p.write_text("{not json", encoding="utf-8")
    assert load_json_object(str(p), {}) == {}


def test_load_json_object_rejects_non_dict(tmp_path):
    # 배열을 주면 빈 dict — load_manifest 의 거울상
    p = tmp_path / "update.json"
    p.write_text(json.dumps([1, 2, 3]), encoding="utf-8")
    assert load_json_object(str(p), {}) == {}


def test_load_json_object_mtime_cache(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"version": "1.0.0"}), encoding="utf-8")
    cache = {}
    first = load_json_object(str(p), cache)
    assert first["version"] == "1.0.0"
    # 파일을 바꾸되 mtime 을 되돌리면 캐시가 살아 있어야 한다
    st = os.stat(str(p))
    p.write_text(json.dumps({"version": "9.9.9"}), encoding="utf-8")
    os.utime(str(p), (st.st_atime, st.st_mtime))
    assert load_json_object(str(p), cache)["version"] == "1.0.0"
    # mtime 이 진짜로 바뀌면 다시 읽는다
    os.utime(str(p), (st.st_atime, st.st_mtime + 10))
    assert load_json_object(str(p), cache)["version"] == "9.9.9"


def test_load_json_object_clears_cache_when_file_disappears(tmp_path):
    p = tmp_path / "update.json"
    p.write_text(json.dumps({"version": "1.0.0"}), encoding="utf-8")
    cache = {}
    load_json_object(str(p), cache)
    os.remove(str(p))
    assert load_json_object(str(p), cache) == {}
    assert "data" not in cache
