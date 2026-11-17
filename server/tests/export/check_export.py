#!/usr/bin/env python3
"""试卷导出接口回归（F09）：真实 HTTP、文件结构、权限与策略。"""
import io
import json
import os
import re
import sqlite3
import sys
import zipfile
import xml.etree.ElementTree as ET
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import urllib.request

proxy_handler = urllib.request.ProxyHandler({})
urllib.request.install_opener(urllib.request.build_opener(proxy_handler))

base = "http://127.0.0.1:" + os.getenv("TEST_PORT", "8099")


def raw_request(path, token=None, method="GET", data=None):
    headers = {}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    if data is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(data, ensure_ascii=False).encode()
    req = Request(base + path, data=data, headers=headers, method=method)
    try:
        response = urlopen(req, timeout=5)
    except HTTPError as error:
        response = error
    with response:
        return response.status, response.headers, response.read()


def json_request(path, token=None, method="GET", data=None):
    status, headers, body = raw_request(path, token, method, data)
    assert headers.get_content_type() == "application/json", (path, status, body[:200])
    return status, json.loads(body), headers


def login(username):
    password = os.getenv("DEMO_" + username.upper() + "_PW", username + "123")
    status, body, _ = json_request(
        "/api/v1/auth/login",
        method="POST",
        data={"username": username, "password": password},
    )
    assert status == 200, "登录失败: " + username
    return body


def create_question(token, content, qtype, options, answer, analysis):
    status, body, _ = json_request(
        "/api/v1/questions",
        token,
        method="POST",
        data={
            "content": content,
            "type": qtype,
            "options": options,
            "answer": answer,
            "analysis": analysis,
            "knowledge_points": ["导出测试"],
        },
    )
    assert status == 201, (status, body)
    return body["question"]["id"]


def create_exam(token, name, questions):
    status, body, _ = json_request(
        "/api/v1/exams",
        token,
        method="POST",
        data={"name": name, "description": "导出回归"},
    )
    assert status == 201, (status, body)
    exam_id = body["exam"]["id"]
    status, body, _ = json_request(
        "/api/v1/exams/" + str(exam_id) + "/questions",
        token,
        method="PUT",
        data={"questions": questions},
    )
    assert status == 200, (status, body)
    return exam_id


def export_request(exam_id, fmt, token, query=""):
    return raw_request(
        "/api/v1/exams/" + str(exam_id) + "/export/" + fmt + query, token
    )


def expect_error(path, token, status, code, method="GET", data=None):
    got, body, _ = json_request(path, token, method=method, data=data)
    assert got == status, (path, got, body)
    assert body.get("code") == code, (path, body)
    return body


def assert_file_headers(headers, exam_id, ext):
    disposition = headers.get("Content-Disposition")
    assert disposition is not None
    assert 'filename="exam-' + str(exam_id) + "." + ext + '"' in disposition
    assert "filename*=UTF-8''" in disposition
    assert "\r" not in disposition and "\n" not in disposition
    assert headers.get("Cache-Control") == "no-store"
    assert headers.get("X-Content-Type-Options") == "nosniff"


def assert_pdf_structure(data):
    assert data.startswith(b"%PDF-1.4\n")
    tail = re.search(rb"startxref\n(\d+)\n%%EOF\n?$", data)
    assert tail is not None, "缺少 startxref/%%EOF"
    xref_offset = int(tail.group(1))
    assert data[xref_offset : xref_offset + 4] == b"xref"

    header = re.match(rb"xref\n0 (\d+)\n", data[xref_offset:])
    assert header is not None, "xref 头非法"
    count = int(header.group(1))
    pos = xref_offset + header.end()
    entries = []
    for _ in range(count):
        match = re.match(rb"(\d{10}) (\d{5}) ([fn]) \n", data[pos:])
        assert match is not None, "xref 条目非法"
        entries.append((int(match.group(1)), match.group(3)))
        pos += match.end()
    assert entries[0][1] == b"f" and entries[0][0] == 0

    trailer = re.search(rb"trailer\n<<(.*?)>>\nstartxref\n", data[pos:], re.S)
    assert trailer is not None, "缺少 trailer"
    trailer_body = trailer.group(1)
    assert re.search(rb"/Size\s+" + str(count).encode() + rb"(?:\s|/)", trailer_body)
    assert re.search(rb"/Root\s+1\s+0\s+R", trailer_body)

    objects = {}
    for obj_no in range(1, count):
        offset = entries[obj_no][0]
        assert data[offset : offset + len(str(obj_no))] == str(obj_no).encode()
        next_offset = entries[obj_no + 1][0] if obj_no + 1 < count else pos
        body = data[offset:next_offset]
        assert re.match(rb"\d+ 0 obj\n", body), ("对象头非法", obj_no)
        objects[obj_no] = body
        stream_at = body.find(b"\nstream\n")
        if stream_at >= 0:
            length_match = re.search(rb"/Length\s+(\d+)", body[:stream_at])
            assert length_match is not None, ("stream 缺少 Length", obj_no)
            stream_start = offset + stream_at + len(b"\nstream\n")
            stream_len = int(length_match.group(1))
            assert data[stream_start + stream_len : stream_start + stream_len + 9] == b"endstream"

    assert any(re.search(rb"/Type\s*/Page(?!s)", body) for body in objects.values())
    assert re.search(rb"/Type\s*/Pages\b", objects[2])


def assert_docx_structure(data):
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        names = set(zf.namelist())
        assert {
            "[Content_Types].xml",
            "_rels/.rels",
            "word/document.xml",
            "word/_rels/document.xml.rels",
        } <= names
        for name in names:
            if name.endswith(".xml") or name.endswith(".rels"):
                ET.fromstring(zf.read(name))
        return zf.read("word/document.xml").decode("utf-8")


admin = login("admin")
admin_token = admin["token"]
teacher = login("teacher")
teacher_token = teacher["token"]
student = login("student")
student_token = student["token"]

exam_ids = []
question_ids = []

try:
    q1 = create_question(
        admin_token,
        "导出题一：2x + 3 = 11",
        "single_choice",
        [{"label": "A", "text": "2"}, {"label": "B", "text": "4"}],
        "B",
        "两边减去 3 得到 2x = 8。",
    )
    q2 = create_question(
        admin_token,
        "导出题二：简述一元一次方程",
        "short_answer",
        [],
        "参考答案二",
        "解析二",
    )
    question_ids.extend([q1, q2])

    exam_id = create_exam(
        admin_token,
        "导出测试卷",
        [
            {"question_id": q1, "position": 1, "score": 5},
            {"question_id": q2, "position": 2, "score": 10},
        ],
    )
    exam_ids.append(exam_id)

    teacher_exam_id = create_exam(
        teacher_token,
        "教师导出卷",
        [{"question_id": q1, "position": 1, "score": 5}],
    )
    exam_ids.append(teacher_exam_id)

    # 401
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export/markdown",
        None,
        401,
        "UNAUTHORIZED",
    )
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export-policy",
        None,
        401,
        "UNAUTHORIZED",
    )

    # 策略默认值/能力
    status, body, _ = json_request(
        "/api/v1/exams/" + str(exam_id) + "/export-policy", admin_token
    )
    assert status == 200
    assert body["export_policy"] == {"allow_export": True, "allow_answers": True}
    assert body["capabilities"]["formats"] == ["pdf", "markdown", "latex", "docx"]
    assert body["capabilities"]["ai_answer"] is False

    # 可见性：student 可读 active，teacher 读他人卷 404
    status, body, _ = json_request(
        "/api/v1/exams/" + str(exam_id) + "/export-policy", student_token
    )
    assert status == 200
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export-policy",
        teacher_token,
        404,
        "EXPORT_NOT_FOUND",
    )

    # Markdown 默认内容
    status, headers, data = export_request(exam_id, "markdown", admin_token)
    assert status == 200
    assert headers.get_content_type() == "text/markdown"
    assert_file_headers(headers, exam_id, "md")
    text = data.decode("utf-8")
    assert "# 导出测试卷" in text
    assert "共 2 题 · 总分 15 分" in text
    assert "导出题一" in text
    assert "- A. 2" in text
    assert "**答案：B**" in text
    assert "**解析：两边减去 3 得到 2x = 8。**" in text
    assert "**答案：参考答案二**" in text
    assert "**解析：解析二**" in text

    # 独立开关
    status, _, data = export_request(
        exam_id, "markdown", admin_token, "?options=0&answers=1&analysis=0"
    )
    text = data.decode("utf-8")
    assert status == 200
    assert "- A. 2" not in text
    assert "**答案：B**" in text
    assert "**解析：两边减去 3 得到 2x = 8。**" not in text

    status, _, data = export_request(
        exam_id, "markdown", admin_token, "?answers=0&analysis=1"
    )
    text = data.decode("utf-8")
    assert status == 200
    assert "**答案：B**" not in text
    assert "**解析：两边减去 3 得到 2x = 8。**" in text

    # PDF / LaTeX / DOCX
    status, headers, data = export_request(exam_id, "pdf", admin_token)
    assert status == 200
    assert headers.get_content_type() == "application/pdf"
    assert_file_headers(headers, exam_id, "pdf")
    assert b"%PDF-1.4" in data and b"%%EOF" in data
    assert b"/UniGB-UCS2-H" in data and b"/STSong-Light" in data
    assert_pdf_structure(data)

    status, headers, data = export_request(exam_id, "latex", admin_token)
    assert status == 200
    assert headers.get_content_type() == "application/x-tex"
    assert_file_headers(headers, exam_id, "tex")
    tex = data.decode("utf-8")
    assert "\\documentclass[11pt,a4paper]{ctexart}" in tex
    assert "\\begin{itemize}" in tex and "\\end{itemize}" in tex
    assert "\\end{document}" in tex

    status, headers, data = export_request(exam_id, "docx", admin_token)
    assert status == 200
    assert headers.get_content_type() == (
        "application/vnd.openxmlformats-officedocument.wordprocessingml.document"
    )
    assert_file_headers(headers, exam_id, "docx")
    xml = assert_docx_structure(data)
    assert "导出测试卷" in xml and "导出题一" in xml

    # 非法格式/参数/路径/方法
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export/PDF",
        admin_token,
        400,
        "EXPORT_UNSUPPORTED_FORMAT",
    )
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export/markdown?answers=yes",
        admin_token,
        400,
        "EXPORT_INVALID_REQUEST",
    )
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export/markdown?ai=1",
        admin_token,
        400,
        "EXPORT_OPTION_UNSUPPORTED",
    )
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export/markdown/extra",
        admin_token,
        404,
        "EXPORT_NOT_FOUND",
    )
    status, body, headers = json_request(
        "/api/v1/exams/" + str(exam_id) + "/export/markdown",
        admin_token,
        method="POST",
    )
    assert status == 405 and body["code"] == "METHOD_NOT_ALLOWED"
    assert headers.get("Allow") == "GET"
    expect_error(
        "/api/v1/exams/99999999/export/markdown",
        admin_token,
        404,
        "EXPORT_NOT_FOUND",
    )

    # 策略写权限与部分更新
    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export-policy",
        student_token,
        403,
        "EXPORT_FORBIDDEN",
        method="PUT",
        data={"allow_export": False},
    )
    status, body, _ = json_request(
        "/api/v1/exams/" + str(exam_id) + "/export-policy",
        admin_token,
        method="PUT",
        data={"allow_export": False},
    )
    assert status == 200
    assert body["export_policy"] == {"allow_export": False, "allow_answers": True}

    expect_error(
        "/api/v1/exams/" + str(exam_id) + "/export/markdown",
        student_token,
        403,
        "EXPORT_FORBIDDEN",
    )
    status, _, _ = export_request(exam_id, "markdown", admin_token)
    assert status == 200

    # 关闭答案后，student 被静默剥离；owner/admin 不受影响
    status, body, _ = json_request(
        "/api/v1/exams/" + str(exam_id) + "/export-policy",
        admin_token,
        method="PUT",
        data={"allow_export": True, "allow_answers": False},
    )
    assert status == 200
    assert body["export_policy"] == {"allow_export": True, "allow_answers": False}

    status, _, data = export_request(exam_id, "markdown", student_token)
    assert status == 200
    text = data.decode("utf-8")
    assert "# 导出测试卷" in text and "导出题一" in text
    assert "答案：" not in text and "解析：" not in text

    status, _, data = export_request(exam_id, "markdown", admin_token)
    assert status == 200
    text = data.decode("utf-8")
    assert "**答案：B**" in text and "**解析：两边减去 3 得到 2x = 8。**" in text

    # 策略请求体校验
    for payload in (
        {"allow_export": "yes"},
        {},
        {"unknown": True},
        {"allow_export": 0},
    ):
        expect_error(
            "/api/v1/exams/" + str(exam_id) + "/export-policy",
            admin_token,
            400,
            "EXPORT_INVALID_REQUEST",
            method="PUT",
            data=payload,
        )

    # 悬空题目：鉴权/策略只读元数据仍可用，文件导出必须 500
    dangling_q = create_question(
        admin_token,
        "悬空引用测试题",
        "short_answer",
        [],
        "答案",
        "解析",
    )
    question_ids.append(dangling_q)
    dangling_exam = create_exam(
        admin_token,
        "悬空引用测试卷",
        [{"question_id": dangling_q, "position": 1, "score": 1}],
    )
    exam_ids.append(dangling_exam)
    db_path = os.getenv("DATABASE_PATH")
    assert db_path, "悬空引用测试需要 DATABASE_PATH"
    with sqlite3.connect(db_path) as conn:
        conn.execute(
            "DELETE FROM qdata WHERE key_name=? AND value_name=?",
            (str(dangling_q), "content"),
        )
        conn.commit()
    status, body, _ = json_request(
        "/api/v1/exams/" + str(dangling_exam) + "/export-policy", admin_token
    )
    assert status == 200
    status, body, _ = json_request(
        "/api/v1/exams/" + str(dangling_exam) + "/export-policy",
        admin_token,
        method="PUT",
        data={"allow_export": True},
    )
    assert status == 200
    expect_error(
        "/api/v1/exams/" + str(dangling_exam) + "/export/markdown",
        admin_token,
        500,
        "EXPORT_STORE_FAILED",
    )

    # 归档：teacher 可导出自己的 archived；student 不可见
    status, body, _ = json_request(
        "/api/v1/exams/" + str(teacher_exam_id),
        admin_token,
        method="PATCH",
        data={"status": "archived"},
    )
    assert status == 200
    status, _, _ = export_request(teacher_exam_id, "markdown", teacher_token)
    assert status == 200
    expect_error(
        "/api/v1/exams/" + str(teacher_exam_id) + "/export/markdown",
        student_token,
        404,
        "EXPORT_NOT_FOUND",
    )
    status, _, _ = export_request(teacher_exam_id, "markdown", admin_token)
    assert status == 200

    # 413：约 180 × 48 KiB 题干，DOCX 中间 XML 超过 8 MiB
    large_content = "超限内容" * 4096  # UTF-8 下 48 KiB/题
    large_questions = []
    for index in range(180):
        qid = create_question(
            admin_token,
            large_content,
            "short_answer",
            [],
            "答案",
            "解析",
        )
        question_ids.append(qid)
        large_questions.append(
            {"question_id": qid, "position": index + 1, "score": 1}
        )
    large_exam = create_exam(admin_token, "超限导出卷", large_questions)
    exam_ids.append(large_exam)
    expect_error(
        "/api/v1/exams/" + str(large_exam) + "/export/docx",
        admin_token,
        413,
        "EXPORT_TOO_LARGE",
    )

    # 文件名清洗：用户内容不进响应头原样
    unsafe_id = create_exam(
        admin_token,
        '导出";/测试卷',
        [{"question_id": q1, "position": 1, "score": 5}],
    )
    exam_ids.append(unsafe_id)
    status, headers, _ = export_request(unsafe_id, "markdown", admin_token)
    assert status == 200
    disposition = headers.get("Content-Disposition")
    assert disposition is not None
    assert "\r" not in disposition and "\n" not in disposition
    assert "filename*=UTF-8''" in disposition

    print("PASS: 导出四格式、独立开关、策略、权限、错误码与文件名清洗")
finally:
    for eid in exam_ids:
        raw_request("/api/v1/exams/" + str(eid), admin_token, method="DELETE")
    for qid in question_ids:
        raw_request("/api/v1/questions/" + str(qid), admin_token, method="DELETE")
