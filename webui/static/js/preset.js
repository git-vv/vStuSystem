/* preset.js - Price preset management page logic */

var g_presetQrcodePaths = [];
var g_addImgPresetId = null;

(function() {
    App.requireLogin(function(session) {
        var nameEl = document.getElementById('user-name');
        if (nameEl) { nameEl.textContent = session.username; }
        loadPresetList();
    }, 0);
})();

function handleLogout() {
    App.apiPost('/api/auth/logout', {}).then(function() {
        window.location.replace('/');
    });
}

/* --- Amount change: lock headcount to 1 when amount is 0 --- */
function onPresetAmountChange() {
    var amountInput = document.getElementById('preset-amount');
    var headcountInput = document.getElementById('preset-headcount');
    if (!amountInput || !headcountInput) { return; }
    var val = parseFloat(amountInput.value);
    if (!isNaN(val) && val < 0.001) {
        headcountInput.value = '1';
        headcountInput.disabled = true;
    } else {
        headcountInput.disabled = false;
    }
}

/* --- Preset list --- */
function loadPresetList() {
    App.apiGet('/api/class/price-presets?_t=' + Date.now()).then(function(res) {
        var listEl = document.getElementById('preset-list');
        if (!listEl) { return; }
        if (res.data.code !== 0) {
            listEl.innerHTML = '<div class="empty-text">加载失败</div>';
            return;
        }
        var presets = res.data.data.presets || [];
        if (presets.length === 0) {
            listEl.innerHTML = '<div class="empty-state"><div class="empty-text">暂无价位预设</div></div>';
            return;
        }
        var html = '';
        for (var i = 0; i < presets.length; i++) {
            var p = presets[i];
            var amt = (typeof p.amount === 'number') ? p.amount.toFixed(2) : p.amount;
            html += '<div class="preset-item" data-preset-id="' + p.id + '">';
            html += '  <div class="preset-item-main">';
            html += '    <div class="preset-item-amount">' + _esc(amt) + '(' + (p.expected_headcount || 1) + '人)</div>';
            html += '    <div class="preset-item-qrcodes">';
            for (var j = 0; j < (p.qrcode_paths || []).length; j++) {
                var path = p.qrcode_paths[j];
                html += '<div class="preset-qrcode-thumb" data-path="' + _esc(path) + '">';
                html += '<img src="' + _esc(path) + '" onclick="App.showQrcode(\'' + _esc(path) + '\')">';
                html += '<button type="button" class="preset-qrcode-remove" onclick="deletePresetQrcode(' + p.id + ', \'' + _esc(path) + '\')">&times;</button>';
                html += '</div>';
            }
            html += '    </div>';
            html += '  </div>';
            html += '  <div class="preset-item-actions">';
            html += '    <button type="button" class="btn btn-primary btn-sm" onclick="openAddPresetImg(' + p.id + ')">添加图片</button>';
            html += '    <button type="button" class="btn btn-danger btn-sm" onclick="deletePreset(' + p.id + ', \'' + _esc(amt) + '\')">删除预设</button>';
            html += '  </div>';
            html += '</div>';
        }
        listEl.innerHTML = html;
    });
}

function handlePresetQrcodeUpload() {
    var input = document.getElementById('preset-qrcode-input');
    if (!input || !input.files || input.files.length === 0) { return; }

    var files = input.files;
    var remaining = 10 - g_presetQrcodePaths.length;
    if (remaining <= 0) {
        App.showToast('最多上传10张二维码', 'error');
        input.value = '';
        return;
    }
    var toUpload = Math.min(files.length, remaining);
    if (files.length > remaining) {
        App.showToast('最多上传10张，已选取前' + toUpload + '张', 'error');
    }

    var uploaded = 0;
    for (var i = 0; i < toUpload; i++) {
        (function(file) {
            var reader = new FileReader();
            reader.onload = function(e) {
                var base64 = e.target.result.split(',')[1];
                App.apiPost('/api/class/upload-qrcode', {
                    filename: file.name,
                    data: base64
                }).then(function(res) {
                    if (res.data.code === 0) {
                        var path = res.data.data.path;
                        g_presetQrcodePaths.push(path);
                        _renderPresetPreview();
                        uploaded++;
                        if (uploaded === toUpload) {
                            App.showToast('上传成功', 'success');
                        }
                    } else {
                        App.showToast(res.data.message || '上传失败', 'error');
                    }
                }).catch(function() {
                    App.showToast('上传失败', 'error');
                });
            };
            reader.readAsDataURL(file);
        })(files[i]);
    }
    input.value = '';
}

function _renderPresetPreview() {
    var preview = document.getElementById('preset-qrcode-preview');
    if (!preview) { return; }
    var html = '';
    for (var i = 0; i < g_presetQrcodePaths.length; i++) {
        var path = g_presetQrcodePaths[i];
        html += '<div class="preset-qrcode-thumb" data-path="' + _esc(path) + '">';
        html += '<img src="' + _esc(path) + '" onclick="App.showQrcode(\'' + _esc(path) + '\')">';
        html += '<button type="button" class="preset-qrcode-remove" onclick="_removePresetPreviewImg(' + i + ')">&times;</button>';
        html += '</div>';
    }
    preview.innerHTML = html;
}

function _removePresetPreviewImg(idx) {
    if (idx < 0 || idx >= g_presetQrcodePaths.length) { return; }
    g_presetQrcodePaths.splice(idx, 1);
    _renderPresetPreview();
}

function addPreset() {
    var amountInput = document.getElementById('preset-amount');
    if (!amountInput) { return; }
    var amountVal = parseFloat(amountInput.value);
    if (isNaN(amountVal) || amountVal < 0) {
        App.showToast('请输入有效金额', 'error');
        return;
    }

    var headcountInput = document.getElementById('preset-headcount');
    var headcountVal = headcountInput ? parseInt(headcountInput.value, 10) : 1;
    /* 0元预设强制1人，且全局只能有一个0元预设 */
    if (amountVal < 0.001) {
        headcountVal = 1;
        if (headcountInput) { headcountInput.value = '1'; }
        /* 检查是否已存在0元预设 */
        var hasZero = false;
        var presetListEl = document.getElementById('preset-list');
        if (presetListEl) {
            var items = presetListEl.querySelectorAll('.preset-item');
            for (var i = 0; i < items.length; i++) {
                var amtText = items[i].querySelector('.preset-item-amount');
                if (amtText) {
                    var m = amtText.textContent.match(/^0\.00/);
                    if (m) { hasZero = true; break; }
                }
            }
        }
        if (hasZero) {
            App.showToast('0元预设已存在，不可重复创建', 'error');
            return;
        }
    } else {
        if (isNaN(headcountVal) || headcountVal < 1) {
            App.showToast('请输入有效的成团人数（>=1）', 'error');
            return;
        }
    }

    App.apiPost('/api/class/price-presets/add', {
        amount: amountVal,
        expected_headcount: headcountVal,
        qrcode_paths: g_presetQrcodePaths
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('预设保存成功', 'success');
            g_presetQrcodePaths = [];
            amountInput.value = '';
            if (headcountInput) { headcountInput.value = '1'; }
            var fileInput = document.getElementById('preset-qrcode-input');
            if (fileInput) { fileInput.value = ''; }
            var previewEl = document.getElementById('preset-qrcode-preview');
            if (previewEl) { previewEl.innerHTML = ''; }
            if (typeof loadPricePresets === 'function') { loadPricePresets(); }
            loadPresetList();
        } else {
            App.showToast(res.data.message || '保存失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function deletePreset(presetId, amount) {
    if (!confirm('确定要删除 ' + amount + ' 价位预设吗？')) { return; }
    App.apiPost('/api/class/price-presets/delete', { preset_id: presetId }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('删除成功', 'success');
            if (typeof loadPricePresets === 'function') { loadPricePresets(); }
            loadPresetList();
        } else if (res.data.code === 4006) {
            /* ERR_PRICE_PRESET_IN_USE: show modal with detailed message */
            var msg = res.data.message || '该价位已被引用，不可删除';
            _showPresetErrorModal('无法删除预设', msg);
        } else {
            App.showToast(res.data.message || '删除失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function _showPresetErrorModal(title, message) {
    var titleEl = document.getElementById('preset-error-title');
    var msgEl = document.getElementById('preset-error-msg');
    if (titleEl) { titleEl.textContent = title; }
    if (msgEl) { msgEl.textContent = message; }
    App.showModal('preset-error-modal');
}

function closePresetErrorModal() {
    App.hideModal('preset-error-modal');
}

function openAddPresetImg(presetId) {
    g_addImgPresetId = presetId;
    var input = document.getElementById('preset-add-img-input');
    if (input) { input.value = ''; input.click(); }
}

function handleAddPresetImgUpload() {
    var input = document.getElementById('preset-add-img-input');
    if (!input || !input.files || input.files.length === 0 || !g_addImgPresetId) { return; }

    var presetId = g_addImgPresetId;
    var files = input.files;
    var totalFiles = files.length;
    var uploaded = 0;
    var failed = 0;

    for (var i = 0; i < totalFiles; i++) {
        (function(file) {
            var reader = new FileReader();
            reader.onload = function(e) {
                var base64 = e.target.result.split(',')[1];
                App.apiPost('/api/class/upload-qrcode', {
                    filename: file.name,
                    data: base64
                }).then(function(res) {
                    if (res.data.code === 0) {
                        var path = res.data.data.path;
                        App.apiPost('/api/class/price-presets/qrcodes/add', {
                            preset_id: presetId,
                            qrcode_path: path
                        }).then(function(res2) {
                            if (res2.data.code === 0) {
                                uploaded++;
                            } else {
                                failed++;
                            }
                            if (uploaded + failed === totalFiles) {
                                if (uploaded > 0) {
                                    App.showToast('添加成功 ' + uploaded + ' 张', 'success');
                                    loadPresetList();
                                }
                                if (failed > 0) {
                                    App.showToast(failed + ' 张添加失败', 'error');
                                }
                            }
                        }).catch(function() {
                            failed++;
                            if (uploaded + failed === totalFiles && uploaded > 0) {
                                loadPresetList();
                            }
                        });
                    } else {
                        failed++;
                        if (uploaded + failed === totalFiles && uploaded > 0) {
                            loadPresetList();
                        }
                    }
                }).catch(function() {
                    failed++;
                    if (uploaded + failed === totalFiles && uploaded > 0) {
                        loadPresetList();
                    }
                });
            };
            reader.readAsDataURL(file);
        })(files[i]);
    }
    input.value = '';
    g_addImgPresetId = null;
}

function deletePresetQrcode(presetId, qrcodePath) {
    App.showConfirm('确定要删除这张图片吗？', function() {
        App.apiPost('/api/class/price-presets/qrcodes/delete', {
            preset_id: presetId,
            qrcode_path: qrcodePath
        }).then(function(res) {
            if (res.data.code === 0) {
                App.showToast('删除成功', 'success');
                if (typeof loadPricePresets === 'function') { loadPricePresets(); }
                loadPresetList();
            } else {
                App.showToast(res.data.message || '删除失败', 'error');
            }
        }).catch(function() {
            App.showToast('网络错误', 'error');
        });
    });
}

/* --- Utility --- */
function _esc(s) {
    if (!s) { return ''; }
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(s));
    return div.innerHTML;
}
