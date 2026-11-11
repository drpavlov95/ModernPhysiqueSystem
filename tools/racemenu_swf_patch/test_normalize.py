from tools.racemenu_swf_patch.normalize import normalize_as_source


def test_normalize_as_source_unifies_crlf_and_trailing_whitespace():
    assert normalize_as_source("a\r\nb   \r\nc\n") == "a\nb\nc\n"
