/* class_create.js - Class create page logic */

var priceCounter = 0;
var g_pricePresets = [];   /* [{id, amount, qrcode_paths:[...]}, ...] */

(function() {
    App.requireLogin(function(session) {
        var nameEl = document.getElementById('user-name');
        if (nameEl) { nameEl.textContent = session.username; }
        loadClassTypes();
        loadPricePresets();
    }, 0);
})();

function handleLogout() {
    App.apiPost('/api/auth/logout', {}).then(function() {
        window.location.replace('/');
    });
}

/* --- Class types --- */
function loadClassTypes() {
    App.apiGet('/api/class/types').then(function(res) {
        var select = document.getElementById('class-type');
        var list = document.getElementById('type-list');
        if (!select || !list) { return; }

        if (res.data.code !== 0) { return; }

        var types = res.data.data.types || [];

        /* Populate class-type select (all types) */
        select.innerHTML = '<option value="">请选择类型</option>';
        for (var i = 0; i < types.length; i++) {
            var opt = document.createElement('option');
            opt.value = types[i].name;
            opt.textContent = types[i].name;
            select.appendChild(opt);
        }

        /* Populate new-type-name select with builtin types as reference */
        var newTypeSelect = document.getElementById('new-type-name');
        if (newTypeSelect) {
            newTypeSelect.innerHTML = '<option value="">选择或输入新类型名称</option>';
            for (var n = 0; n < types.length; n++) {
                if (types[n].is_builtin) {
                    var refOpt = document.createElement('option');
                    refOpt.value = types[n].name;
                    refOpt.textContent = types[n].name;
                    newTypeSelect.appendChild(refOpt);
                }
            }
        }

        /* Render type list (custom types only, builtin shown in dropdown) */
        var customTypes = [];
        for (var j = 0; j < types.length; j++) {
            if (!types[j].is_builtin) {
                customTypes.push(types[j]);
            }
        }

        if (customTypes.length === 0) {
            list.innerHTML = '<div class="empty-state"><div class="empty-text">暂无自定义类型，内置类型请从下拉框选择</div></div>';
            return;
        }

        var html = '';
        for (var m = 0; m < customTypes.length; m++) {
            var t = customTypes[m];
            html += '<div class="list-item list-item-clickable" data-type-id="' + t.id + '" data-type-name="' + _esc(t.name) + '" data-builtin="0" onclick="selectClassType(\'' + _esc(t.name) + '\')">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(t.name) + '</div>';
            html += '  </div>';
            html += '  <div class="item-actions">';
            html += '    <button class="btn btn-danger btn-sm" onclick="event.stopPropagation(); deleteClassType(' + t.id + ', \'' + _esc(t.name) + '\')">删除</button>';
            html += '  </div>';
            html += '</div>';
        }
        list.innerHTML = html;

        /* Bind long-press on custom items */
        var items = list.querySelectorAll('.list-item[data-builtin="0"]');
        for (var k = 0; k < items.length; k++) {
            (function(el) {
                var tid = parseInt(el.getAttribute('data-type-id'), 10);
                var tname = el.getAttribute('data-type-name');
                App.bindLongPress(el, function(e) {
                    App.showContextMenu(e, [
                        { label: '删除类型', danger: true, action: function() { deleteClassType(tid, tname); } }
                    ]);
                });
            })(items[k]);
        }
    });
}

function selectClassType(name) {
    var select = document.getElementById('class-type');
    if (select) {
        select.value = name;
        App.showToast('已选择: ' + name, 'success');
    }
}

function addClassType() {
    var selectEl = document.getElementById('new-type-name');
    var inputEl = document.getElementById('new-type-name-input');
    var name = '';
    if (selectEl && selectEl.value) {
        name = selectEl.value.trim();
    }
    if (!name && inputEl) {
        name = inputEl.value.trim();
    }
    if (!name) {
        App.showToast('请选择或输入类型名称', 'error');
        return;
    }

    App.apiPost('/api/class/types/add', { name: name }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('添加成功', 'success');
            if (selectEl) { selectEl.value = ''; }
            if (inputEl) { inputEl.value = ''; }
            loadClassTypes();
        } else {
            App.showToast(res.data.message || '添加失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function deleteClassType(typeId, typeName) {
    if (!confirm('确定要删除类型 "' + typeName + '" 吗？')) { return; }

    App.apiPost('/api/class/types/delete', { id: typeId }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('删除成功', 'success');
            loadClassTypes();
        } else {
            App.showToast(res.data.message || '删除失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Price presets --- */
function loadPricePresets() {
    return App.apiGet('/api/class/price-presets').then(function(res) {
        if (res.data.code === 0) {
            g_pricePresets = res.data.data.presets || [];
        } else {
            g_pricePresets = [];
        }
        /* Refresh all existing price-item dropdowns */
        var selects = document.querySelectorAll('.price-preset-select');
        for (var i = 0; i < selects.length; i++) {
            _refillPresetSelect(selects[i], selects[i].value);
        }
    }).catch(function() {
        g_pricePresets = [];
    });
}

function _refillPresetSelect(selectEl, keepValue) {
    if (!selectEl) { return; }
    var cur = keepValue || selectEl.value || '';
    var html = '<option value="">请选择价位</option>';
    for (var i = 0; i < g_pricePresets.length; i++) {
        var p = g_pricePresets[i];
        var amt = (typeof p.amount === 'number') ? p.amount.toFixed(2) : p.amount;
        var pAmt = (typeof p.amount === 'number') ? p.amount : parseFloat(p.amount);
        if (pAmt < 0.001) { continue; } /* 0元预设为定金报名专用，不作为普通价位 */
        var hc = p.expected_headcount || 1;
        html += '<option value="' + p.id + '">' + _esc(amt) + '(' + hc + '人)</option>';
    }
    selectEl.innerHTML = html;
    if (cur) { selectEl.value = cur; }
}

/* --- Price items --- */
function addPriceItem() {
    priceCounter++;
    var id = priceCounter;
    var container = document.getElementById('price-list');

    var div = document.createElement('div');
    div.className = 'price-item';
    div.id = 'price-item-' + id;
    div.innerHTML =
        '<button type="button" class="price-remove" onclick="removePriceItem(' + id + ')">&times;</button>' +
        '<div class="form-row">' +
        '  <div class="form-group">' +
        '    <label>活动名称</label>' +
        '    <input type="text" class="form-control price-activity" placeholder="如：全程班">' +
        '  </div>' +
        '  <div class="form-group">' +
        '    <label>价位预设</label>' +
        '    <select class="form-control price-preset-select"><option value="">请选择价位</option></select>' +
        '  </div>' +
        '</div>';

    container.appendChild(div);
    var sel = div.querySelector('.price-preset-select');
    _refillPresetSelect(sel, '');
}

function removePriceItem(id) {
    var el = document.getElementById('price-item-' + id);
    if (el) { el.remove(); }
}

/* --- Create class --- */
function handleCreateClass(e) {
    e.preventDefault();
    var errEl = document.getElementById('create-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var startTime = document.getElementById('start-time').value;
    var endTime = document.getElementById('end-time').value;
    var classType = document.getElementById('class-type').value;
    var capacity = parseInt(document.getElementById('enrollment-capacity').value, 10);
    var description = document.getElementById('description').value.trim();

    if (!startTime) { _showCreateError('请选择开始日期'); return; }
    if (!endTime) { _showCreateError('请选择结束日期'); return; }
    if (!classType) { _showCreateError('请选择班级类型'); return; }
    if (!capacity || capacity <= 0) { _showCreateError('招生名额必须大于0'); return; }
    if (startTime > endTime) { _showCreateError('开始日期不能晚于结束日期'); return; }

    /* Collect prices: {activity_name, preset_id} */
    var prices = [];
    var priceItems = document.querySelectorAll('.price-item');
    for (var i = 0; i < priceItems.length; i++) {
        var item = priceItems[i];
        var activityName = item.querySelector('.price-activity').value.trim();
        var presetId = parseInt(item.querySelector('.price-preset-select').value, 10);
        if (!activityName) {
            _showCreateError('请填写第 ' + (i + 1) + ' 个价位项的活动名称');
            return;
        }
        if (!presetId || isNaN(presetId)) {
            _showCreateError('请选择第 ' + (i + 1) + ' 个价位项的价位预设');
            return;
        }
        prices.push({
            activity_name: activityName,
            preset_id: presetId
        });
    }
    if (prices.length === 0) {
        _showCreateError('请至少添加一个价位项');
        return;
    }
    /* Reject duplicate preset_id in same class */
    var seen = {};
    for (var k = 0; k < prices.length; k++) {
        if (seen[prices[k].preset_id]) {
            _showCreateError('同一班级不能选择重复的价位预设');
            return;
        }
        seen[prices[k].preset_id] = true;
    }

    var btn = document.getElementById('create-btn');
    btn.disabled = true;
    btn.textContent = '创建中...';

    App.apiPost('/api/class/create', {
        start_time: startTime,
        end_time: endTime,
        class_type: classType,
        enrollment_capacity: capacity,
        description: description,
        prices: prices
    }).then(function(res) {
        btn.disabled = false;
        btn.textContent = '创建班级';
        if (res.data.code === 0) {
            App.showToast('班级创建成功: ' + (res.data.data.class_name || ''), 'success', 3000);
            /* Reset form */
            document.getElementById('create-form').reset();
            document.getElementById('price-list').innerHTML = '';
            priceCounter = 0;
            /* Jump to home page after a short delay so the toast is visible */
            setTimeout(function() { window.location.replace('/'); }, 1500);
        } else {
            _showCreateError(res.data.message || '创建失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '创建班级';
        _showCreateError('网络错误，请重试');
    });
}

function _showCreateError(msg) {
    var errEl = document.getElementById('create-error');
    if (errEl) {
        errEl.textContent = msg;
        errEl.classList.add('show');
    }
}

/* --- Utility --- */
function _esc(s) {
    if (!s) { return ''; }
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(s));
    return div.innerHTML;
}
