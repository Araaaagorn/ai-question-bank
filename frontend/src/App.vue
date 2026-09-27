<script setup>
// 骨架入口页：后续按模块拆分为 views/ 下页面（登录、题库、试卷、解答树…）
</script>

<template>
  <main class="page">
    <h1>AI+题库智能教学平台</h1>
    <p>前端骨架已就绪。接入后端：<code>npm run dev</code> 后访问 /api/v1/health。</p>
  </main>
</template>

<style scoped>
.page {
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  font-family: system-ui, sans-serif;
}
</style>
