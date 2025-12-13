// Poll for new analysis results
let pollInterval;
let lastAnalysisId = null;
let lastAnalysisTimestamp = null;

function getHealthLevelClass(metric, value) {
    const val = Number(value);
    if (Number.isNaN(val)) return '';
    if (metric === 'calories') {
        if (val >= 400) return 'level-red';
        if (val >= 200) return 'level-yellow';
        return 'level-blue';
    }
    if (metric === 'gi') {
        if (val >= 70) return 'level-red';
        if (val >= 56) return 'level-yellow';
        return 'level-blue';
    }
    if (metric === 'gl') {
        if (val >= 20) return 'level-red';
        if (val >= 11) return 'level-yellow';
        return 'level-blue';
    }
    return '';
}

function startPolling() {
    console.log('🔄 开始轮询最新分析结果...');
    // Check for new results every 2 seconds
    pollInterval = setInterval(checkForNewData, 2000);
}

async function checkForNewData() {
    try {
        console.log('🔍 正在检查新的分析数据...');
        const response = await fetch('/api/latest-analysis?t=' + Date.now());
        console.log('📡 响应状态:', response.status);
        if (response.ok) {
            const result = await response.json();
            console.log('📡 API 返回:', result);
            if (result.success && result.data) {
                console.log(
                    '✅ 找到数据，当前 ID:',
                    result.data.id,
                    '上一条 ID:',
                    lastAnalysisId
                );
                const isNew = result.data.id !== lastAnalysisId;
                const isUpdated =
                    !!result.data.timestamp &&
                    result.data.timestamp !== lastAnalysisTimestamp;
                if (isNew || isUpdated) {
                    console.log('🖼️ 图像路径:', result.data.image.path);
                    lastAnalysisId = result.data.id;
                    lastAnalysisTimestamp = result.data.timestamp || null;
                    displayResults(result.data);
                } else {
                    console.log('⏸️ 分析 ID 未变化，无需更新');
                }
            } else {
                // No data available yet
                console.log('⚠️ 暂无分析数据');
                updateStatus(
                    '系统已就绪 - 等待设备数据',
                    '暂无分析结果'
                );
            }
        } else {
            console.error('❌ API 请求失败:', response.status);
        }
    } catch (error) {
        console.error('❌ 检查新数据时出错:', error);
        updateStatus(
            '连接异常 - 请检查服务器',
            '上次错误：' + new Date().toLocaleTimeString()
        );
    }
}

function updateStatus(statusText, lastUpdateText) {
    document.getElementById('statusText').textContent = statusText;
    if (lastUpdateText) {
        document.getElementById('lastUpdate').textContent = lastUpdateText;
    }
}

function showLoading() {}
function hideLoading() {}

function displayResults(data) {
    console.log('📋 正在展示分析结果:', data);
    console.log('🖼️ 图像数据:', data.image);
    console.log('📂 图像完整路径:', data.image.path);

    const processing =
        data.status === 'processing' ||
        (data.analysis && data.analysis.foodType === 'Processing');
    if (processing) {
        updateStatus('正在分析...', '开始时间：' + new Date(data.timestamp).toLocaleString());
    } else {
        updateStatus(
            '分析完成',
            '上次分析：' + new Date(data.timestamp).toLocaleString()
        );
    }

    const resultsSection = document.getElementById('resultsSection');
    const { analysis, weight, image, timestamp } = data;

    const imageHtml = `<img src="${image.path}" alt="食物图片：${image.originalName}" class="food-image" 
                            onload="console.log('✅ 图片加载成功：${image.path}')" 
                            onerror="console.error('❌ 图片加载失败：${image.path}')" />`;

        let html = `
            <h2>🔍 食物分析结果</h2>
            <div style="margin: 20px 0;">${imageHtml}</div>
            <div style="margin-bottom: 20px;">
                <h3>📊 食物识别</h3>
                <p><strong>重量：</strong> ${weight}g</p>
                <p><strong>图片文件：</strong> ${image.originalName}</p>
                <p><strong>图片路径：</strong> <a href="${image.path}" target="_blank">${image.path}</a></p>`;

        if (processing) {
                html += `
                <p><strong>状态：</strong> 分析中...</p>
                <p><strong>食物类型：</strong> Processing</p>`;
        } else {
                html += `
                <p><strong>食物类型：</strong> ${analysis.foodType}</p>
                <p><strong>置信度：</strong> ${(analysis.confidence * 100).toFixed(1)}%</p>
                <h3>🥗 营养信息（每 ${weight}g）</h3>
                <div class="nutrition-grid">
                    <div class="nutrition-item">
                        <div class="value ${getHealthLevelClass('calories', analysis.nutrition.calories)}">${analysis.nutrition.calories}</div>
                        <div class="label">热量</div>
                    </div>
                    <div class="nutrition-item">
                        <div class="value">${analysis.nutrition.protein}g</div>
                        <div class="label">蛋白质</div>
                    </div>
                    <div class="nutrition-item">
                        <div class="value">${analysis.nutrition.carbs}g</div>
                        <div class="label">碳水</div>
                    </div>
                    <div class="nutrition-item">
                        <div class="value">${analysis.nutrition.fat}g</div>
                        <div class="label">脂肪</div>
                    </div>
                    <div class="nutrition-item">
                        <div class="value">${analysis.nutrition.fiber}g</div>
                        <div class="label">膳食纤维</div>
                    </div>
                    <div class="nutrition-item">
                        <div class="value ${getHealthLevelClass('gi', analysis.nutrition.GI)}">${analysis.nutrition.GI}</div>
                        <div class="label">GI（升糖指数）</div>
                    </div>
                    <div class="nutrition-item">
                        <div class="value ${getHealthLevelClass('gl', analysis.nutrition.GL)}">${analysis.nutrition.GL}</div>
                        <div class="label">GL（升糖负荷）</div>
                    </div>
                </div>
                <div class="suggestions">
                    <h3>💡 健康建议</h3>
                    <ul>
                        ${analysis.healthSuggestions.map(suggestion => `<li>${suggestion}</li>`).join('')}
                    </ul>
                    <h3>🍽️ 菜品推荐</h3>
                    <ul>
                        ${(analysis.dishSuggestions || []).map(suggestion => `<li>${suggestion}</li>`).join('')}
                    </ul>
                </div>`;
        }

        html += `</div>`;

        resultsSection.innerHTML = html;

    console.log('🎯 结果区域已更新，准备显示...');
    resultsSection.style.display = 'block';

}


// Initialize the system
document.addEventListener('DOMContentLoaded', function () {
    console.log('🍎 食物分析系统已就绪！');
    console.log('🔧 正在设置初始状态...');
    updateStatus(
        '系统已就绪 - 等待设备数据',
        '页面加载时间：' + new Date().toLocaleTimeString()
    );
    console.log('🔄 开始轮询系统...');
    startPolling();
    console.log('✅ 系统初始化完成！');
});

// Cleanup on page unload
window.addEventListener('beforeunload', function () {
    if (pollInterval) {
        clearInterval(pollInterval);
    }
});
