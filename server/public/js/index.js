// Poll for new analysis results
let pollInterval;
let lastAnalysisId = null;
let lastAnalysisTimestamp = null;

const PROFILE_STORAGE_KEY = 'nutrivision.userProfile.v1';

function getDefaultProfile() {
    return {
        age: null,
        sex: '',
        heightCm: null,
        weightKg: null,
        allergens: [],
        goals: { fatLoss: false, muscleGain: false },
        conditions: {
            diabetes: false,
            hypertension: false,
            kidneyDisease: false,
            gout: false,
            allergy: false,
        },
    };
}

function safeParseJSON(text, fallback) {
    try {
        return JSON.parse(text);
    } catch {
        return fallback;
    }
}

function normalizeAllergensText(text) {
    const raw = String(text || '')
        .replace(/，/g, ',')
        .split(',')
        .map(s => s.trim())
        .filter(Boolean);
    // de-dup
    return Array.from(new Set(raw)).slice(0, 30);
}

function loadProfileFromLocalStorage() {
    const raw = localStorage.getItem(PROFILE_STORAGE_KEY);
    if (!raw) return getDefaultProfile();
    const parsed = safeParseJSON(raw, null);
    if (!parsed || typeof parsed !== 'object') return getDefaultProfile();

    const d = getDefaultProfile();
    return {
        ...d,
        ...parsed,
        goals: { ...d.goals, ...(parsed.goals || {}) },
        conditions: { ...d.conditions, ...(parsed.conditions || {}) },
        allergens: Array.isArray(parsed.allergens)
            ? parsed.allergens.map(a => String(a)).filter(Boolean)
            : d.allergens,
    };
}

function saveProfileToLocalStorage(profile) {
    localStorage.setItem(PROFILE_STORAGE_KEY, JSON.stringify(profile));
}

function setProfileStatus(text) {
    const el = document.getElementById('profileStatus');
    if (el) el.textContent = text || '';
}

function readProfileFromForm() {
    const age = Number(document.getElementById('profileAge')?.value);
    const heightCm = Number(document.getElementById('profileHeightCm')?.value);
    const weightKg = Number(document.getElementById('profileWeightKg')?.value);
    const sex = String(document.getElementById('profileSex')?.value || '');
    const allergensText = String(document.getElementById('profileAllergens')?.value || '');

    const profile = getDefaultProfile();
    profile.age = Number.isFinite(age) && age > 0 ? Math.round(age) : null;
    profile.heightCm = Number.isFinite(heightCm) && heightCm > 0 ? Math.round(heightCm) : null;
    profile.weightKg = Number.isFinite(weightKg) && weightKg > 0 ? Math.round(weightKg * 10) / 10 : null;
    profile.sex = sex;
    profile.allergens = normalizeAllergensText(allergensText);
    profile.goals.fatLoss = !!document.getElementById('goalFatLoss')?.checked;
    profile.goals.muscleGain = !!document.getElementById('goalMuscleGain')?.checked;
    profile.conditions.diabetes = !!document.getElementById('condDiabetes')?.checked;
    profile.conditions.hypertension = !!document.getElementById('condHypertension')?.checked;
    profile.conditions.kidneyDisease = !!document.getElementById('condKidney')?.checked;
    profile.conditions.gout = !!document.getElementById('condGout')?.checked;
    profile.conditions.allergy = !!document.getElementById('condAllergy')?.checked;
    return profile;
}

function fillProfileForm(profile) {
    const p = profile || getDefaultProfile();
    const ageEl = document.getElementById('profileAge');
    const sexEl = document.getElementById('profileSex');
    const heightEl = document.getElementById('profileHeightCm');
    const weightEl = document.getElementById('profileWeightKg');
    const allergensEl = document.getElementById('profileAllergens');

    if (ageEl) ageEl.value = p.age ?? '';
    if (sexEl) sexEl.value = p.sex || '';
    if (heightEl) heightEl.value = p.heightCm ?? '';
    if (weightEl) weightEl.value = p.weightKg ?? '';
    if (allergensEl) allergensEl.value = (p.allergens || []).join(', ');

    const goalFatLoss = document.getElementById('goalFatLoss');
    const goalMuscleGain = document.getElementById('goalMuscleGain');
    if (goalFatLoss) goalFatLoss.checked = !!p.goals?.fatLoss;
    if (goalMuscleGain) goalMuscleGain.checked = !!p.goals?.muscleGain;

    const condDiabetes = document.getElementById('condDiabetes');
    const condHypertension = document.getElementById('condHypertension');
    const condKidney = document.getElementById('condKidney');
    const condGout = document.getElementById('condGout');
    const condAllergy = document.getElementById('condAllergy');
    if (condDiabetes) condDiabetes.checked = !!p.conditions?.diabetes;
    if (condHypertension) condHypertension.checked = !!p.conditions?.hypertension;
    if (condKidney) condKidney.checked = !!p.conditions?.kidneyDisease;
    if (condGout) condGout.checked = !!p.conditions?.gout;
    if (condAllergy) condAllergy.checked = !!p.conditions?.allergy;
}

async function postProfileToServer(profile) {
    const resp = await fetch('/api/profile', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(profile),
    });
    if (!resp.ok) {
        throw new Error('Profile API failed: ' + resp.status);
    }
    const data = await resp.json();
    if (!data || !data.success) {
        throw new Error(data?.error || 'Profile API returned failure');
    }
    return data.data;
}

function initProfileForm() {
    // Load from localStorage first (since server is non-persistent)
    const profile = loadProfileFromLocalStorage();
    fillProfileForm(profile);

    const btn = document.getElementById('profileSaveBtn');
    if (btn) {
        btn.addEventListener('click', async () => {
            try {
                setProfileStatus('正在保存...');
                const p = readProfileFromForm();
                saveProfileToLocalStorage(p);
                await postProfileToServer(p);
                setProfileStatus('已保存（将用于后续菜品推荐）');
            } catch (e) {
                console.error('❌ 保存用户画像失败:', e);
                setProfileStatus('保存失败：请检查服务器连接');
            }
        });
    }

    // Best-effort: if server already has a profile, reflect it.
    // (localStorage仍然作为主要来源；不做自动覆盖)
    fetch('/api/profile')
        .then(r => (r.ok ? r.json() : null))
        .then(j => {
            if (j && j.success && j.data) {
                setProfileStatus('服务器已加载画像（用于后续菜品推荐）');
            }
        })
        .catch(() => {
            // ignore
        });
}

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
    initProfileForm();
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
