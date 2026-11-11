#!/usr/bin/env python3
"""试卷接口回归（F03）：真实 HTTP + JSON 结构验证，不依赖 grep 匹配。"""
import json
import os
import sys
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import urllib.request
proxy_handler = urllib.request.ProxyHandler({})
urllib.request.install_opener(urllib.request.build_opener(proxy_handler))

base = "http://127.0.0.1:" + os.getenv("TEST_PORT", "8099")

def request(path, token=None, method="GET", data=None):
    headers = {}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    if data is not None:
        headers["Content-Type"] = "application/json"
        data = json.dumps(data).encode()
    req = Request(base + path, data=data, headers=headers, method=method)
    try:
        response = urlopen(req, timeout=5)
    except HTTPError as error:
        response = error
    with response:
        assert response.headers.get_content_type() == "application/json"
        return response.status, json.loads(response.read()), response.headers

def login(username):
    password = os.getenv("DEMO_" + username.upper() + "_PW", username + "123")
    status, body, _ = request("/api/v1/auth/login", method="POST",
                              data={"username": username, "password": password})
    assert status == 200, "登录失败"
    assert isinstance(body["token"], str) and body["token"]
    assert isinstance(body["user"]["id"], int) and body["user"]["id"] > 0
    return body

def check_exam_list_item(item):
    assert set(item) == {"id", "name", "description", "teacher_id", "status",
                         "created_at", "updated_at", "question_count"}
    assert isinstance(item["id"], int) and item["id"] > 0
    assert isinstance(item["name"], str) and item["name"]
    assert isinstance(item["description"], str)
    assert isinstance(item["teacher_id"], int) and item["teacher_id"] > 0
    assert item["status"] in ("active", "archived")
    assert isinstance(item["question_count"], int) and item["question_count"] >= 0

def check_exam_detail(exam):
    assert set(exam) == {"id", "name", "description", "teacher_id", "status",
                         "created_at", "updated_at", "questions"}
    assert isinstance(exam["id"], int) and exam["id"] > 0
    assert isinstance(exam["name"], str) and exam["name"]
    assert isinstance(exam["description"], str)
    assert isinstance(exam["teacher_id"], int) and exam["teacher_id"] > 0
    assert exam["status"] in ("active", "archived")
    assert isinstance(exam["questions"], list)

mode = sys.argv[1]

if mode == "list":
    admin = login("admin"); admin_token = admin["token"]
    teacher = login("teacher"); teacher_token = teacher["token"]
    student = login("student"); student_token = student["token"]

    # 初始为空
    for token in (admin_token, teacher_token, student_token):
        status, body, _ = request("/api/v1/exams", token)
        assert status == 200 and body == {"exams": []}

    # 401（未登录优先）
    for path in ("/api/v1/exams", "/api/v1/exams/1"):
        status, body, _ = request(path)
        assert status == 401, path
        assert set(body) == {"error", "code"} and body["code"] == "UNAUTHORIZED"

    # student 写 → 403（先于资源存在性）
    for method, path, data in (("POST", "/api/v1/exams", {"name": "x"}),
                               ("PUT", "/api/v1/exams/1", {"name": "x"}),
                               ("PATCH", "/api/v1/exams/1", {"status": "active"}),
                               ("DELETE", "/api/v1/exams/1", None)):
        status, body, _ = request(path, student_token, method=method, data=data)
        assert status == 403, (method, status)
        assert body["code"] == "EXAM_FORBIDDEN"

    # admin 建 A 卷、teacher 建 B 卷
    status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                              data={"name": "A卷", "description": "admin 的卷"})
    assert status == 201
    a = body["exam"]; check_exam_detail(a)
    assert a["teacher_id"] == admin["user"]["id"]
    assert a["name"] == "A卷" and a["status"] == "active" and a["questions"] == []

    status, body, _ = request("/api/v1/exams", teacher_token, method="POST",
                              data={"name": "B卷", "description": ""})
    assert status == 201
    b = body["exam"]; check_exam_detail(b)
    assert b["teacher_id"] == teacher["user"]["id"]

    aid, bid = a["id"], b["id"]

    # 列表按角色过滤 + 字段结构
    status, body, _ = request("/api/v1/exams", admin_token)
    assert status == 200 and {e["id"] for e in body["exams"]} == {aid, bid}
    for e in body["exams"]:
        check_exam_list_item(e)
    status, body, _ = request("/api/v1/exams", teacher_token)
    assert status == 200 and [e["id"] for e in body["exams"]] == [bid]
    status, body, _ = request("/api/v1/exams", student_token)
    assert status == 200 and {e["id"] for e in body["exams"]} == {aid, bid}

    # teacher 读他人试卷 → 404（不泄露）；admin 读他人试卷 → 200
    status, body, _ = request("/api/v1/exams/" + str(aid), teacher_token)
    assert status == 404 and body["code"] == "EXAM_NOT_FOUND"
    status, body, _ = request("/api/v1/exams/" + str(bid), admin_token)
    assert status == 200 and body["exam"]["id"] == bid

    # teacher 写他人试卷 → 403
    status, body, _ = request("/api/v1/exams/" + str(aid), teacher_token,
                              method="PUT", data={"name": "x"})
    assert status == 403 and body["code"] == "EXAM_FORBIDDEN"

    # 归档 B：学生列表不再出现、详情 404；teacher 本人仍可见
    status, body, _ = request("/api/v1/exams/" + str(bid), teacher_token,
                              method="PATCH", data={"status": "archived"})
    assert status == 200 and body["exam"]["status"] == "archived"
    status, body, _ = request("/api/v1/exams/" + str(bid), student_token)
    assert status == 404
    status, body, _ = request("/api/v1/exams", student_token)
    assert status == 200 and {e["id"] for e in body["exams"]} == {aid}
    status, body, _ = request("/api/v1/exams/" + str(bid), teacher_token)
    assert status == 200

    # 非法 id → 404；方法错误 → 405 + Allow
    for invalid in ("999999", "0", "abc", "1.5", "%2B1", "1/extra"):
        status, body, _ = request("/api/v1/exams/" + invalid, admin_token)
        assert status == 404, (invalid, status)
        assert body["code"] == "EXAM_NOT_FOUND"
    status, body, headers = request("/api/v1/exams", admin_token,
                                    method="PATCH", data={"status": "active"})
    assert status == 405 and headers.get("Allow") == "GET, POST"
    status, body, headers = request("/api/v1/exams/" + str(aid), admin_token,
                                    method="POST")
    assert status == 405 and headers.get("Allow") == "GET, PUT, PATCH, DELETE"

    # 清理
    assert request("/api/v1/exams/" + str(aid), admin_token, method="DELETE")[0] == 200
    assert request("/api/v1/exams/" + str(bid), teacher_token, method="DELETE")[0] == 200
    status, body, _ = request("/api/v1/exams", admin_token)
    assert status == 200 and body == {"exams": []}
    print("PASS: 列表/详情结构、角色过滤、401/403/404/405、归档可见性、用例自清理")

elif mode == "crud":
    admin = login("admin"); admin_token = admin["token"]
    teacher = login("teacher"); teacher_token = teacher["token"]
    student = login("student"); student_token = student["token"]

    created = []
    try:
        # 创建校验矩阵
        bad = [
            {},
            {"name": ""},
            {"name": "x", "id": 5},
            {"name": "x", "status": "active"},
            {"name": "x", "description": 123},
            {"name": 123},
        ]
        for payload in bad:
            status, body, _ = request("/api/v1/exams", admin_token,
                                      method="POST", data=payload)
            assert status == 400, (payload, status)
            assert body["code"] == "EXAM_INVALID_REQUEST"

        # 201 成功
        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "期中卷", "description": "描述"})
        assert status == 201
        exam = body["exam"]; check_exam_detail(exam)
        assert exam["name"] == "期中卷" and exam["description"] == "描述"
        assert exam["teacher_id"] == admin["user"]["id"]
        assert exam["status"] == "active" and exam["questions"] == []
        eid = exam["id"]; created.append(eid)

        # PUT 部分更新：未提供字段保留
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PUT", data={"name": "新名"})
        assert status == 200
        assert body["exam"]["name"] == "新名"
        assert body["exam"]["description"] == "描述"
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PUT", data={"description": "新描述"})
        assert status == 200
        assert body["exam"]["description"] == "新描述"
        assert body["exam"]["name"] == "新名"

        # PUT 错误路径
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PUT", data={})
        assert status == 400 and body["code"] == "EXAM_INVALID_REQUEST"
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PUT", data={"name": "x", "status": "active"})
        assert status == 400
        status, body, _ = request("/api/v1/exams/999999", admin_token,
                                  method="PUT", data={"name": "x"})
        assert status == 404 and body["code"] == "EXAM_NOT_FOUND"

        # PATCH 状态切换 + archived 编辑封锁
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PATCH", data={"status": "bogus"})
        assert status == 400 and body["code"] == "EXAM_INVALID_REQUEST"
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PATCH", data={"status": "archived"})
        assert status == 200 and body["exam"]["status"] == "archived"
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PUT", data={"name": "不该成功"})
        assert status == 409 and body["code"] == "EXAM_ARCHIVED"
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PATCH", data={"status": "active"})
        assert status == 200
        status, body, _ = request("/api/v1/exams/" + str(eid), admin_token,
                                  method="PUT", data={"name": "可改了"})
        assert status == 200 and body["exam"]["name"] == "可改了"

        # 权限：student 写 / teacher 写他人 → 403
        for method, path, data in (("PUT", "/api/v1/exams/" + str(eid), {"name": "x"}),
                                   ("PATCH", "/api/v1/exams/" + str(eid), {"status": "active"}),
                                   ("DELETE", "/api/v1/exams/" + str(eid), None)):
            status, body, _ = request(path, student_token, method=method, data=data)
            assert status == 403 and body["code"] == "EXAM_FORBIDDEN"
        status, body, _ = request("/api/v1/exams/" + str(eid), teacher_token,
                                  method="PUT", data={"name": "x"})
        assert status == 403 and body["code"] == "EXAM_FORBIDDEN"

        # 405 + Allow
        status, body, headers = request("/api/v1/exams/" + str(eid), admin_token,
                                        method="POST")
        assert status == 405 and headers.get("Allow") == "GET, PUT, PATCH, DELETE"
    finally:
        for e in created:
            request("/api/v1/exams/" + str(e), admin_token, method="DELETE")
    print("PASS: 创建校验矩阵、201、PUT 部分更新、PATCH 状态切换、archived 409、恢复可改、403/405")

elif mode == "questions":
    admin = login("admin"); admin_token = admin["token"]
    teacher = login("teacher"); teacher_token = teacher["token"]
    student = login("student"); student_token = student["token"]

    created = []
    try:
        # 准备题目：seed 题 1/2/3 均 active（short_answer/single_choice 混合）
        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "编排卷", "description": ""})
        assert status == 201
        eid = body["exam"]["id"]; created.append(eid)

        # 1. 整体替换成功：数组顺序为权威，position 归一化
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="PUT",
                                  data={"questions": [
                                      {"question_id": 2, "position": 2, "score": 10},
                                      {"question_id": 1, "position": 1, "score": 5},
                                  ]})
        assert status == 200, status
        qs = body["exam"]["questions"]
        assert [q["question_id"] for q in qs] == [2, 1]
        assert [q["position"] for q in qs] == [1, 2]
        # 内联题目字段存在（含 answer/analysis，视图不裁剪）
        assert all(set(q) == {"question_id", "position", "score", "content", "type",
                              "options", "knowledge_points", "answer", "analysis"}
                   for q in qs)
        assert all(q["answer"] and q["analysis"] for q in qs)

        # 2. 追加单题：position 置末尾
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="POST",
                                  data={"question_id": 3, "score": 8})
        assert status == 201, status
        qs = body["exam"]["questions"]
        assert [q["question_id"] for q in qs] == [2, 1, 3]
        assert [q["position"] for q in qs] == [1, 2, 3]

        # 3. 追加接口不接受 position → 400
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="POST",
                                  data={"question_id": 1, "score": 8, "position": 1})
        assert status == 400 and body["code"] == "EXAM_INVALID_REQUEST"

        # 4. 重复 question_id → 400（PUT 整体替换重复 + POST 重复追加）
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="PUT",
                                  data={"questions": [
                                      {"question_id": 1, "position": 1, "score": 5},
                                      {"question_id": 1, "position": 2, "score": 6},
                                  ]})
        assert status == 400 and body["code"] == "EXAM_INVALID_LAYOUT"
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="POST",
                                  data={"question_id": 3, "score": 8})
        assert status == 400 and body["code"] == "EXAM_INVALID_LAYOUT"

        # 5. 不存在的题目 → 400 EXAM_INVALID_QUESTION
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="PUT",
                                  data={"questions": [{"question_id": 99999,
                                                       "position": 1, "score": 5}]})
        assert status == 400 and body["code"] == "EXAM_INVALID_QUESTION"

        # 6. position 跳号 → 400 EXAM_INVALID_LAYOUT
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="PUT",
                                  data={"questions": [{"question_id": 1,
                                                       "position": 2, "score": 5}]})
        assert status == 400 and body["code"] == "EXAM_INVALID_LAYOUT"

        # 7. score 0/负数/超上限 → 400 EXAM_INVALID_REQUEST
        for bad_score in (0, -1, 1001):
            status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                      admin_token, method="POST",
                                      data={"question_id": 1, "score": bad_score})
            assert status == 400, (bad_score, status)
            assert body["code"] == "EXAM_INVALID_REQUEST"

        # 8. 题目数超限（201 个元素）→ 400（数量校验先于其他校验）
        oversize = {"questions": [{"question_id": 1, "position": i, "score": 1}
                                  for i in range(1, 202)]}
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  admin_token, method="PUT", data=oversize)
        assert status == 400 and body["code"] == "EXAM_INVALID_REQUEST"

        # 9. 移除后重排：当前 [2,1,3] → 移除 1 → [2,3]，position 1,2
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions/1",
                                  admin_token, method="DELETE")
        assert status == 200, status
        qs = body["exam"]["questions"]
        assert [q["question_id"] for q in qs] == [2, 3]
        assert [q["position"] for q in qs] == [1, 2]
        # 移除不存在的引用 → 404
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions/1",
                                  admin_token, method="DELETE")
        assert status == 404 and body["code"] == "EXAM_NOT_FOUND"

        # 10. archived → 409（PUT/POST/DELETE questions 均封锁）
        assert request("/api/v1/exams/" + str(eid), admin_token, method="PATCH",
                       data={"status": "archived"})[0] == 200
        for method, path, data in (
            ("PUT", "/api/v1/exams/" + str(eid) + "/questions",
             {"questions": [{"question_id": 1, "position": 1, "score": 5}]}),
            ("POST", "/api/v1/exams/" + str(eid) + "/questions",
             {"question_id": 1, "score": 5}),
            ("DELETE", "/api/v1/exams/" + str(eid) + "/questions/2", None),
        ):
            status, body, _ = request(path, admin_token, method=method, data=data)
            assert status == 409, (method, status)
            assert body["code"] == "EXAM_ARCHIVED"
        # 恢复 active 后可编辑
        assert request("/api/v1/exams/" + str(eid), admin_token, method="PATCH",
                       data={"status": "active"})[0] == 200

        # 11. student 403（写操作先于资源存在性）
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  student_token, method="PUT",
                                  data={"questions": [{"question_id": 1,
                                                       "position": 1, "score": 5}]})
        assert status == 403 and body["code"] == "EXAM_FORBIDDEN"
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  student_token, method="POST",
                                  data={"question_id": 1, "score": 5})
        assert status == 403 and body["code"] == "EXAM_FORBIDDEN"
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions/2",
                                  student_token, method="DELETE")
        assert status == 403 and body["code"] == "EXAM_FORBIDDEN"

        # 12. 他人试卷 403（teacher 操作 admin 的试卷）
        status, body, _ = request("/api/v1/exams/" + str(eid) + "/questions",
                                  teacher_token, method="PUT",
                                  data={"questions": [{"question_id": 1,
                                                       "position": 1, "score": 5}]})
        assert status == 403 and body["code"] == "EXAM_FORBIDDEN"

        # 405 + Allow
        status, body, headers = request("/api/v1/exams/" + str(eid) + "/questions",
                                        admin_token, method="GET")
        assert status == 405 and headers.get("Allow") == "PUT, POST"
        status, body, headers = request("/api/v1/exams/" + str(eid) + "/questions/2",
                                        admin_token, method="POST")
        assert status == 405 and headers.get("Allow") == "DELETE"
    finally:
        for e in created:
            request("/api/v1/exams/" + str(e), admin_token, method="DELETE")
    print("PASS: 整体替换重排、追加、移除重排、重复/不存在/跳号/score/超限、archived 409、403、405")

elif mode == "lifecycle":
    admin = login("admin"); admin_token = admin["token"]
    teacher = login("teacher"); teacher_token = teacher["token"]

    exams_created = []
    question_created = []
    try:
        # 1. 归档后可查、恢复 active 后可编辑（往返）
        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "往返卷", "description": ""})
        assert status == 201
        e1 = body["exam"]["id"]; exams_created.append(e1)

        status, body, _ = request("/api/v1/exams/" + str(e1), admin_token,
                                  method="PATCH", data={"status": "archived"})
        assert status == 200 and body["exam"]["status"] == "archived"
        # archived 后 admin 详情仍可见
        status, body, _ = request("/api/v1/exams/" + str(e1), admin_token)
        assert status == 200 and body["exam"]["status"] == "archived"
        # archived 禁止编辑 → 409
        status, body, _ = request("/api/v1/exams/" + str(e1), admin_token,
                                  method="PUT", data={"name": "不该成功"})
        assert status == 409 and body["code"] == "EXAM_ARCHIVED"
        # 恢复 active 后可编辑
        status, body, _ = request("/api/v1/exams/" + str(e1), admin_token,
                                  method="PATCH", data={"status": "active"})
        assert status == 200
        status, body, _ = request("/api/v1/exams/" + str(e1), admin_token,
                                  method="PUT", data={"name": "已恢复"})
        assert status == 200 and body["exam"]["name"] == "已恢复"

        # 2. archived 也可删除，重复删除 404
        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "待删卷", "description": ""})
        assert status == 201
        e2 = body["exam"]["id"]; exams_created.append(e2)
        assert request("/api/v1/exams/" + str(e2), admin_token, method="PATCH",
                       data={"status": "archived"})[0] == 200
        status, body, _ = request("/api/v1/exams/" + str(e2), admin_token,
                                  method="DELETE")
        assert status == 200 and body == {"status": "ok"}
        # 删除后详情 404、重复删除 404
        status, body, _ = request("/api/v1/exams/" + str(e2), admin_token)
        assert status == 404 and body["code"] == "EXAM_NOT_FOUND"
        status, body, _ = request("/api/v1/exams/" + str(e2), admin_token,
                                  method="DELETE")
        assert status == 404 and body["code"] == "EXAM_NOT_FOUND"

        # 3. 删除题目联动：从试卷移除引用并重排 position
        status, body, _ = request("/api/v1/questions", admin_token, method="POST",
                                  data={"content": "联动测试题", "type": "short_answer",
                                        "options": [], "answer": "答案",
                                        "knowledge_points": ["测试"]})
        assert status == 201
        q_new = body["question"]["id"]; question_created.append(q_new)

        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "联动卷", "description": ""})
        assert status == 201
        e3 = body["exam"]["id"]; exams_created.append(e3)

        # 编排为 [seed1, q_new, seed2]（position 1,2,3）
        status, body, _ = request("/api/v1/exams/" + str(e3) + "/questions",
                                  admin_token, method="PUT",
                                  data={"questions": [
                                      {"question_id": 1, "position": 1, "score": 5},
                                      {"question_id": q_new, "position": 2, "score": 6},
                                      {"question_id": 2, "position": 3, "score": 7},
                                  ]})
        assert status == 200
        assert [q["question_id"] for q in body["exam"]["questions"]] == [1, q_new, 2]

        # 删除题目 q_new → 试卷应移除 q_new，seed2 接替为 position 2
        status, body, _ = request("/api/v1/questions/" + str(q_new), admin_token,
                                  method="DELETE")
        assert status == 200
        status, body, _ = request("/api/v1/exams/" + str(e3), admin_token)
        assert status == 200
        qs = body["exam"]["questions"]
        assert [q["question_id"] for q in qs] == [1, 2]
        assert [q["position"] for q in qs] == [1, 2]
        # 题目已从 qdata 删除
        status, _, _ = request("/api/v1/questions/" + str(q_new), admin_token)
        assert status == 404
    finally:
        for e in exams_created:
            request("/api/v1/exams/" + str(e), admin_token, method="DELETE")
        for q in question_created:
            request("/api/v1/questions/" + str(q), admin_token, method="DELETE")
    print("PASS: 归档往返、archived 可删除、重复删除 404、删除题目联动移除并重排")

elif mode == "student":
    admin = login("admin"); admin_token = admin["token"]
    teacher = login("teacher"); teacher_token = teacher["token"]
    student = login("student"); student_token = student["token"]

    exams_created = []
    try:
        # 建 3 张卷：A active（编排 2 题）、B archived、C active 但空
        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "A卷", "description": ""})
        assert status == 201
        a = body["exam"]["id"]; exams_created.append(a)
        status, body, _ = request("/api/v1/exams/" + str(a) + "/questions",
                                  admin_token, method="PUT",
                                  data={"questions": [
                                      {"question_id": 2, "position": 2, "score": 10},
                                      {"question_id": 1, "position": 1, "score": 5},
                                  ]})
        assert status == 200

        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "B卷", "description": ""})
        assert status == 201
        b = body["exam"]["id"]; exams_created.append(b)
        assert request("/api/v1/exams/" + str(b), admin_token, method="PATCH",
                       data={"status": "archived"})[0] == 200

        status, body, _ = request("/api/v1/exams", admin_token, method="POST",
                                  data={"name": "C卷", "description": ""})
        assert status == 201
        c = body["exam"]["id"]; exams_created.append(c)

        # 1. student 列表只含 active（A、C），不含 archived（B）
        status, body, _ = request("/api/v1/exams", student_token)
        assert status == 200
        assert {e["id"] for e in body["exams"]} == {a, c}

        # 2. student 详情 A：字段完整 + answer/analysis 存在 + position 升序
        status, body, _ = request("/api/v1/exams/" + str(a), student_token)
        assert status == 200
        qs = body["exam"]["questions"]
        assert [q["position"] for q in qs] == [1, 2]
        assert [q["question_id"] for q in qs] == [2, 1]
        for q in qs:
            assert set(q) == {"question_id", "position", "score", "content",
                              "type", "options", "knowledge_points", "answer",
                              "analysis"}
            assert isinstance(q["answer"], str) and q["answer"]
            assert isinstance(q["analysis"], str) and q["analysis"]

        # 3. student 详情 B（archived）→ 404，不泄露存在性
        status, body, _ = request("/api/v1/exams/" + str(b), student_token)
        assert status == 404 and body["code"] == "EXAM_NOT_FOUND"

        # 4. 空试卷 student 读 200 且 questions=[]
        status, body, _ = request("/api/v1/exams/" + str(c), student_token)
        assert status == 200 and body["exam"]["questions"] == []

        # 5. student 任何写操作 → 403
        for method, path, data in (
            ("POST", "/api/v1/exams", {"name": "x"}),
            ("PUT", "/api/v1/exams/" + str(a), {"name": "x"}),
            ("PATCH", "/api/v1/exams/" + str(a), {"status": "archived"}),
            ("DELETE", "/api/v1/exams/" + str(a), None),
        ):
            status, body, _ = request(path, student_token, method=method, data=data)
            assert status == 403, (method, status)
            assert body["code"] == "EXAM_FORBIDDEN"

        # 6. teacher 读他人试卷：按 docs/exams.md 权限矩阵「teacher 仅本人」→ 404，
        #    但写他人试卷 → 403（先于存在性）。
        status, body, _ = request("/api/v1/exams/" + str(a), teacher_token)
        assert status == 404 and body["code"] == "EXAM_NOT_FOUND"
        status, body, _ = request("/api/v1/exams/" + str(a), teacher_token,
                                  method="PUT", data={"name": "x"})
        assert status == 403 and body["code"] == "EXAM_FORBIDDEN"
    finally:
        for e in exams_created:
            request("/api/v1/exams/" + str(e), admin_token, method="DELETE")
    print("PASS: student 列表过滤、archived 404、字段含 answer/analysis、position 升序、空卷 200、写 403")

else:
    raise ValueError(mode)
