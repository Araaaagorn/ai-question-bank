// 骨架页：调用后端健康检查接口展示服务状态
fetch("/api/v1/health")
  .then((r) => r.json())
  .then((d) => {
    document.getElementById("health").textContent =
      d.status === "ok" ? "✔ 正常" : "异常";
  })
  .catch(() => {
    document.getElementById("health").textContent = "✘ 无法连接后端";
  });
