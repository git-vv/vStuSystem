/* registration.js - Registration/payment page logic */

var _amountManuallyModified = false;
var _classStartTime = '';
var _classEndTime = '';

(function() {
    App.requireLogin(function(session) {
        if (session) {
            var nameEl = document.getElementById('user-name');
            if (nameEl) { nameEl.textContent = session.username; }
            /* Pre-fill teacher name for teacher role */
            if (session.role === 1) {
                var teacherInput = document.getElementById('reg-teacher-name');
                if (teacherInput && !teacherInput.value) {
                    teacherInput.value = session.display_name || session.username;
                }
            }
        }
        loadClasses();
    });

    /* Initialize first student row */
    addStudentRow();

    /* Search on enter */
    var keywordInput = document.getElementById('class-keyword');
    if (keywordInput) {
        keywordInput.addEventListener('keyup', function(e) {
            if (e.key === 'Enter') { loadClasses(); }
        });
    }

    /* Date change triggers amount recalculation */
    var startDateEl = document.getElementById('reg-start-date');
    var endDateEl = document.getElementById('reg-end-date');
    if (startDateEl) { startDateEl.addEventListener('change', onPeriodOrPriceChange); }
    if (endDateEl) { endDateEl.addEventListener('change', onPeriodOrPriceChange); }

    /* Amount manual edit tracking */
    var amountEl = document.getElementById('reg-actual-amount');
    if (amountEl) {
        amountEl.addEventListener('input', function() {
            _amountManuallyModified = true;
        });
    }
})();

function handleLogout() {
    App.apiPost('/api/auth/logout', {}).then(function() {
        window.location.replace('/');
    });
}

/* --- Class list --- */
function loadClasses() {
    var container = document.getElementById('class-list');
    if (!container) { return; }
    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/registration/classes').then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var classes = res.data.data ? (res.data.data.classes || []) : [];
        var keyword = (document.getElementById('class-keyword').value || '').trim().toLowerCase();

        if (keyword) {
            classes = classes.filter(function(c) {
                return (c.class_name || '').toLowerCase().indexOf(keyword) >= 0 ||
                       (c.class_type || '').toLowerCase().indexOf(keyword) >= 0;
            });
        }

        if (classes.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128218;</div><div class="empty-text">暂无可报名的班级，请等待管理员创建</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < classes.length; i++) {
            var c = classes[i];
            var used = parseFloat(c.enrollment_used) || 0;
            var remain = c.enrollment_capacity - used;
            var statusBadge = remain > 0.001
                ? '<span class="item-badge badge-green">可报名(' + _formatEnrollment(remain) + '名额)</span>'
                : '<span class="item-badge badge-red">已满</span>';

            html += '<div class="list-item" onclick="selectClass(' + c.class_id + ')">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(c.class_name) + statusBadge + '</div>';
            html += '    <div class="item-sub">';
            html += '      类型: ' + _esc(c.class_type) + ' | ';
            html += '      ' + App.formatDate(c.start_time) + ' ~ ' + App.formatDate(c.end_time);
            html += '    </div>';
            html += '  </div>';
            html += '</div>';
        }
        container.innerHTML = html;
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

/* --- Select class & show detail --- */
function selectClass(classId) {
    App.apiGet('/api/registration/class?id=' + classId).then(function(res) {
        if (res.data.code !== 0) {
            App.showToast(res.data.message || '获取班级详情失败', 'error');
            return;
        }

        var data = res.data.data;
        document.getElementById('reg-class-id').value = data.id;

        /* Store class period for date pickers */
        _classStartTime = data.start_time || '';
        _classEndTime = data.end_time || '';

        /* Show class detail */
        var titleEl = document.getElementById('selected-class-title');
        if (titleEl) { titleEl.textContent = data.class_name; }

        var detailEl = document.getElementById('class-detail');
        if (detailEl) {
            var used = parseFloat(data.enrollment_used) || 0;
            var remain = data.enrollment_capacity - used;
            var html = '<div style="font-size:0.9rem; color:#4a5568; line-height:1.8;">';
            html += '<div>类型: ' + _esc(data.class_type) + '</div>';
            html += '<div>时间: ' + App.formatDate(data.start_time) + ' ~ ' + App.formatDate(data.end_time) + '</div>';
            html += '<div>名额: ' + _formatEnrollment(used) + ' / ' + data.enrollment_capacity;
            if (remain <= 0.001) { html += ' <span style="color:#e53e3e;">(已满)</span>'; }
            html += '</div>';
            if (data.description) { html += '<div>描述: ' + _esc(data.description) + '</div>'; }
            if (data.bed_remain !== undefined && data.bed_remain >= 0) {
                html += '<div>剩余床位: ' + data.bed_remain + '</div>';
                _bedRemain = data.bed_remain;
            } else {
                _bedRemain = -1;
            }
            html += '</div>';

            /* Price list (text only, QR codes shown after registration) */
            var prices = data.prices || [];
            if (prices.length > 0) {
                html += '<div style="margin-top:12px;">';
                html += '<div style="font-weight:600; margin-bottom:6px;">价位信息:</div>';
                for (var i = 0; i < prices.length; i++) {
                    var p = prices[i];
                    var amt = (typeof p.price === 'number') ? p.price.toFixed(2) : p.price;
                    var hc = p.expected_headcount || 1;
                    html += '<div style="padding:6px 0; border-bottom:1px solid #f0f0f0;">';
                    html += '<div>' + _esc(p.activity_name || '默认') + ': <strong>' + amt + '(' + hc + '人)</strong></div>';
                    html += '</div>';
                }
                html += '</div>';
            }

            detailEl.innerHTML = html;
        }

        /* Populate price select */
        var priceSelect = document.getElementById('reg-price-id');
        if (priceSelect) {
            priceSelect.innerHTML = '<option value="">请选择报名方式</option>';
            var prices = data.prices || [];
            window._classPrices = prices;
            window._depositQrcodePaths = data.deposit_qrcode_paths || [];
            for (var k = 0; k < prices.length; k++) {
                var optAmt = (typeof prices[k].price === 'number') ? prices[k].price.toFixed(2) : prices[k].price;
                var optHc = prices[k].expected_headcount || 1;
                var opt = document.createElement('option');
                opt.value = prices[k].id;
                opt.textContent = (prices[k].activity_name || '默认') + ' - ' + optAmt + '(' + optHc + '人)';
                priceSelect.appendChild(opt);
            }
        }

        /* Update headcount hint */
        onPriceChange();

        /* Initialize date pickers with class period */
        var startDateEl = document.getElementById('reg-start-date');
        var endDateEl = document.getElementById('reg-end-date');
        if (startDateEl) {
            startDateEl.min = _classStartTime;
            startDateEl.max = _classEndTime;
            startDateEl.value = _classStartTime;
        }
        if (endDateEl) {
            endDateEl.min = _classStartTime;
            endDateEl.max = _classEndTime;
            endDateEl.value = _classEndTime;
        }
        _amountManuallyModified = false;

        /* Show period and amount groups */
        var periodGroup = document.getElementById('period-group');
        var amountGroup = document.getElementById('amount-group');
        if (periodGroup) { periodGroup.style.display = 'block'; }
        if (amountGroup) { amountGroup.style.display = 'block'; }

        /* Calculate suggested amount */
        calculateSuggestedAmount();

        /* Reset student list to single row */
        var listEl = document.getElementById('student-list');
        if (listEl) { listEl.innerHTML = ''; }
        addStudentRow();

        /* Switch view */
        document.getElementById('step-select').style.display = 'none';
        document.getElementById('step-register').style.display = 'block';
        window.scrollTo(0, 0);
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function backToClassList() {
    document.getElementById('step-select').style.display = 'block';
    document.getElementById('step-register').style.display = 'none';
    window.scrollTo(0, 0);
}

/* --- Register --- */
var _pendingRegBody = null;
var _bedRemain = -1; /* -1 = no bed resource, >=0 = bed remain count */
var _studentRowCounter = 0;

function _getPriceHeadcount(priceId) {
    if (!priceId || !window._classPrices) { return 1; }
    for (var i = 0; i < window._classPrices.length; i++) {
        if (window._classPrices[i].id === priceId) {
            var h = parseInt(window._classPrices[i].expected_headcount, 10);
            return (h && h > 0) ? h : 1;
        }
    }
    return 1;
}

function onPriceChange() {
    var priceSelect = document.getElementById('reg-price-id');
    var hintEl = document.getElementById('price-headcount-hint');
    if (!priceSelect || !hintEl) { return; }
    var priceId = parseInt(priceSelect.value, 10) || 0;
    if (!priceId) { hintEl.style.display = 'none'; return; }
    var h = _getPriceHeadcount(priceId);
    if (h > 1) {
        hintEl.textContent = '该价位需 ' + h + ' 人同时报名';
        hintEl.style.color = '#dd6b20';
    } else {
        hintEl.textContent = '该价位为单人可用，无需成团';
        hintEl.style.color = '#38a169';
    }
    hintEl.style.display = 'block';
    onPeriodOrPriceChange();
}

function onPeriodOrPriceChange() {
    calculateSuggestedAmount();
}

function calculateSuggestedAmount() {
    var classId = parseInt(document.getElementById('reg-class-id').value, 10) || 0;
    var start = (document.getElementById('reg-start-date') || {}).value || '';
    var end = (document.getElementById('reg-end-date') || {}).value || '';
    var priceId = parseInt((document.getElementById('reg-price-id') || {}).value, 10) || 0;
    if (!classId || !start || !end || !priceId) { return; }

    App.apiGet('/api/class/calculate-amount?class_id=' + classId + '&start=' + start + '&end=' + end + '&price_id=' + priceId).then(function(res) {
        if (res.data.code === 0 && res.data.data) {
            var suggested = res.data.data.suggested_amount;
            var hintEl = document.getElementById('suggested-amount-hint');
            if (hintEl) {
                hintEl.textContent = '系统建议：' + suggested.toFixed(2) + ' 元';
            }
            if (!_amountManuallyModified) {
                var amountEl = document.getElementById('reg-actual-amount');
                if (amountEl) { amountEl.value = suggested.toFixed(2); }
            }
        }
    }).catch(function() {
        /* silently ignore calculation errors */
    });
}

function _formatEnrollment(val) {
    if (typeof val !== 'number') { val = parseFloat(val) || 0; }
    val = Math.round(val * 100) / 100;
    if (val === Math.floor(val)) {
        return Math.floor(val).toString();
    }
    return val.toFixed(2);
}

function onPayTypeChange() {
    var payTypeEl = document.querySelector('input[name="pay-type"]:checked');
    var payType = payTypeEl ? payTypeEl.value : 'scan';
    var depositGroup = document.getElementById('deposit-amount-group');
    var hintEl = document.getElementById('pay-type-hint');
    if (payType === 'deposit') {
        if (depositGroup) { depositGroup.style.display = 'block'; }
        if (hintEl) {
            hintEl.textContent = '定金报名：先付定金锁定名额，后续可在班级管理中补缴尾款转全额。定金方式需选择 0 元预设对应价位。';
            hintEl.style.color = '#3182ce';
        }
    } else {
        if (depositGroup) { depositGroup.style.display = 'none'; }
        if (hintEl) { hintEl.textContent = ''; }
    }
}

function addStudentRow() {
    var listEl = document.getElementById('student-list');
    if (!listEl) { return; }
    if (_studentRowCounter >= 50) {
        App.showToast('单次最多 50 个学生', 'error');
        return;
    }
    var idx = _studentRowCounter++;
    var row = document.createElement('div');
    row.className = 'student-row';
    row.dataset.rowIndex = idx;

    var html = '';
    html += '<div class="student-row-header">';
    html += '  <span>学生 #' + (idx + 1) + '</span>';
    if (idx > 0) {
        html += '  <button type="button" class="btn btn-danger btn-sm" onclick="removeStudentRow(' + idx + ')">删除</button>';
    }
    html += '</div>';
    html += '<div class="student-row-body">';
    html += '  <div class="form-group"><label>姓名 <span class="required">*</span></label>';
    html += '    <input type="text" class="form-control" id="reg-student-name-' + idx + '" placeholder="学生姓名"></div>';
    html += '  <div class="form-group"><label>性别 <span class="required">*</span></label>';
    html += '    <div class="radio-group">';
    html += '      <label><input type="radio" name="reg-gender-' + idx + '" value="male" checked> 男</label>';
    html += '      <label><input type="radio" name="reg-gender-' + idx + '" value="female"> 女</label>';
    html += '    </div></div>';
    html += '  <div class="form-group"><label>家长电话</label>';
    html += '    <input type="tel" class="form-control" id="reg-parent-phone-' + idx + '" placeholder="选填"></div>';
    html += '  <div class="form-group"><label>过敏情况</label>';
    html += '    <div class="radio-group">';
    html += '      <label><input type="radio" name="reg-allergy-' + idx + '" value="0" checked onchange="onAllergyChange(' + idx + ')"> 无</label>';
    html += '      <label><input type="radio" name="reg-allergy-' + idx + '" value="1" onchange="onAllergyChange(' + idx + ')"> 有</label>';
    html += '    </div></div>';
    html += '  <div class="form-group" id="allergy-desc-group-' + idx + '" style="display:none;"><label>过敏描述 <span class="required">*</span></label>';
    html += '    <input type="text" class="form-control" id="reg-allergy-desc-' + idx + '" placeholder="过敏描述"></div>';
    html += '  <div class="form-group"><label>是否需要床位</label>';
    html += '    <div class="radio-group">';
    html += '      <label><input type="radio" name="reg-need-bed-' + idx + '" value="0" checked> 不需要</label>';
    html += '      <label><input type="radio" name="reg-need-bed-' + idx + '" value="1"> 需要</label>';
    html += '    </div></div>';
    html += '</div>';

    row.innerHTML = html;
    listEl.appendChild(row);
}

function removeStudentRow(idx) {
    var listEl = document.getElementById('student-list');
    if (!listEl) { return; }
    var rows = listEl.querySelectorAll('.student-row');
    for (var i = 0; i < rows.length; i++) {
        if (parseInt(rows[i].dataset.rowIndex, 10) === idx) {
            listEl.removeChild(rows[i]);
            return;
        }
    }
}

function onAllergyChange(idx) {
    var checked = document.querySelector('input[name="reg-allergy-' + idx + '"]:checked');
    var descGroup = document.getElementById('allergy-desc-group-' + idx);
    if (descGroup && checked) {
        descGroup.style.display = (checked.value === '1') ? 'block' : 'none';
    }
}

function collectStudents() {
    var listEl = document.getElementById('student-list');
    if (!listEl) { return null; }
    var rows = listEl.querySelectorAll('.student-row');
    var students = [];
    for (var i = 0; i < rows.length; i++) {
        var idx = parseInt(rows[i].dataset.rowIndex, 10);
        var name = (document.getElementById('reg-student-name-' + idx) || {}).value || '';
        name = name.trim();
        if (!name) { _showRegError('请填写第 ' + (i + 1) + ' 个学生姓名'); return null; }
        var genderEl = document.querySelector('input[name="reg-gender-' + idx + '"]:checked');
        if (!genderEl) { _showRegError('请选择第 ' + (i + 1) + ' 个学生性别'); return null; }
        var phone = (document.getElementById('reg-parent-phone-' + idx) || {}).value || '';
        phone = phone.trim();
        if (phone && !/^\d{7,15}$/.test(phone)) {
            _showRegError('第 ' + (i + 1) + ' 个学生家长电话格式不正确（7-15位数字）');
            return null;
        }
        var allergyEl = document.querySelector('input[name="reg-allergy-' + idx + '"]:checked');
        var hasAllergy = allergyEl ? parseInt(allergyEl.value, 10) : 0;
        var allergyDesc = (document.getElementById('reg-allergy-desc-' + idx) || {}).value || '';
        allergyDesc = allergyDesc.trim();
        if (hasAllergy === 1 && !allergyDesc) {
            _showRegError('请填写第 ' + (i + 1) + ' 个学生的过敏描述');
            return null;
        }
        var bedEl = document.querySelector('input[name="reg-need-bed-' + idx + '"]:checked');
        var needBed = bedEl ? parseInt(bedEl.value, 10) : 0;
        students.push({
            student_name: name,
            student_gender: genderEl.value,
            parent_phone: phone,
            has_allergy: hasAllergy,
            allergy_desc: allergyDesc,
            need_bed: needBed
        });
    }
    if (students.length === 0) { _showRegError('请至少添加一个学生'); return null; }
    return students;
}

function showHeadcountMismatchModal(expected, actual) {
    var msgEl = document.getElementById('headcount-mismatch-msg');
    if (msgEl) {
        msgEl.textContent = '该价位必须 ' + expected + ' 个学生同时报名才能享用，当前已登记 ' + actual + ' 个，请调整学生数后重试。';
    }
    App.showModal('headcount-mismatch-modal');
}

function handleRegister(e) {
    e.preventDefault();
    var errEl = document.getElementById('reg-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var classId = parseInt(document.getElementById('reg-class-id').value, 10);
    var priceId = parseInt(document.getElementById('reg-price-id').value, 10) || 0;
    var teacherName = document.getElementById('reg-teacher-name').value.trim();
    var otherInfo = document.getElementById('reg-other-info').value.trim();

    if (!teacherName) { _showRegError('请输入负责教师姓名'); return; }
    if (!priceId) { _showRegError('请选择报名方式'); return; }

    var students = collectStudents();
    if (!students) { return; }

    /* Headcount mismatch check */
    var expectedHeadcount = _getPriceHeadcount(priceId);
    if (expectedHeadcount > 0 && students.length !== expectedHeadcount) {
        showHeadcountMismatchModal(expectedHeadcount, students.length);
        return;
    }

    /* Get period dates */
    var startDate = (document.getElementById('reg-start-date') || {}).value || '';
    var endDate = (document.getElementById('reg-end-date') || {}).value || '';

    /* Validate dates */
    if (startDate && endDate && startDate > endDate) {
        _showRegError('上课开始日期不能晚于结束日期');
        return;
    }

    _pendingRegBody = {
        class_id: classId,
        price_id: priceId,
        teacher_name: teacherName,
        other_info: otherInfo,
        students: students,
        student_start_date: startDate,
        student_end_date: endDate
    };

    var payTypeEl = document.querySelector('input[name="pay-type"]:checked');
    var payType = payTypeEl ? payTypeEl.value : 'scan';
    if (payType === 'deposit') {
        var depositInput = document.getElementById('reg-deposit-amount');
        var depositAmount = depositInput ? parseFloat(depositInput.value) : NaN;
        if (isNaN(depositAmount) || depositAmount < 0) {
            _showRegError('请输入有效的定金金额');
            return;
        }
        _pendingRegBody.is_deposit = 1;
        _pendingRegBody.deposit_amount = depositAmount;
        showDepositModal(depositAmount);
    } else {
        /* Full payment: include actual_amount */
        var actualAmountInput = document.getElementById('reg-actual-amount');
        var actualAmount = actualAmountInput ? parseFloat(actualAmountInput.value) : NaN;
        if (isNaN(actualAmount) || actualAmount < 0) {
            _showRegError('请输入有效的实际金额');
            return;
        }
        _pendingRegBody.actual_amount = actualAmount;

        /* Show amount confirmation modal before payment */
        var confirmText = document.getElementById('confirm-amount-text');
        if (confirmText) { confirmText.textContent = actualAmount.toFixed(2); }
        App.showModal('amount-confirm-modal');
    }
}

function confirmAmountAndProceed() {
    App.hideModal('amount-confirm-modal');
    if (!_pendingRegBody) { return; }
    showPaymentModal();
}

function _renderQrcodeGallery(listId, indicatorId, qrcodePaths) {
    var qrcodeList = document.getElementById(listId);
    var indicator = document.getElementById(indicatorId);
    qrcodeList.innerHTML = '';
    if (qrcodePaths && qrcodePaths.length > 0) {
        for (var j = 0; j < qrcodePaths.length; j++) {
            var item = document.createElement('div');
            item.className = 'qrcode-gallery-item';
            var img = document.createElement('img');
            img.className = 'qrcode-gallery-img';
            img.src = qrcodePaths[j];
            img.alt = '收款二维码 ' + (j + 1);
            img.onclick = (function(src) { return function() { App.showQrcode(src); }; })(qrcodePaths[j]);
            item.appendChild(img);
            var label = document.createElement('div');
            label.className = 'qrcode-gallery-label';
            label.textContent = '二维码 ' + (j + 1);
            item.appendChild(label);
            qrcodeList.appendChild(item);
        }

        if (qrcodePaths.length > 1) {
            indicator.style.display = '';
            indicator.textContent = '1 / ' + qrcodePaths.length;
            qrcodeList.onscroll = function() {
                var scrollLeft = qrcodeList.scrollLeft;
                var firstItem = qrcodeList.querySelector('.qrcode-gallery-item');
                var itemWidth = firstItem ? (firstItem.offsetWidth + 10) : 1;
                var idx = Math.round(scrollLeft / itemWidth) + 1;
                if (idx < 1) { idx = 1; }
                if (idx > qrcodePaths.length) { idx = qrcodePaths.length; }
                indicator.textContent = idx + ' / ' + qrcodePaths.length;
            };
        } else {
            indicator.style.display = 'none';
        }
    } else {
        qrcodeList.innerHTML = '<div style="color:#718096; padding:20px;">暂无收款二维码，请联系管理员上传</div>';
        indicator.style.display = 'none';
    }
}

function showPaymentModal() {
    var priceSelect = document.getElementById('reg-price-id');
    var selectedOption = priceSelect.options[priceSelect.selectedIndex];
    var priceInfo = document.getElementById('payment-price-info');

    /* Show price info */
    if (priceSelect.value && selectedOption) {
        priceInfo.textContent = '报名方式: ' + selectedOption.textContent;
    } else {
        priceInfo.textContent = '未选择报名方式，请确认后付款';
    }

    /* Show QR codes for selected price in scrollable gallery */
    var priceId = parseInt(priceSelect.value, 10) || 0;
    var qrcodePaths = [];
    if (priceId > 0 && window._classPrices) {
        for (var i = 0; i < window._classPrices.length; i++) {
            var p = window._classPrices[i];
            if (p.id === priceId && p.qrcode_paths && p.qrcode_paths.length > 0) {
                qrcodePaths = p.qrcode_paths;
                break;
            }
        }
    }

    _renderQrcodeGallery('payment-qrcode-list', 'qrcode-indicator', qrcodePaths);

    App.showModal('qrcode-modal');
}

function showDepositModal(depositAmount) {
    var amountInfo = document.getElementById('deposit-amount-info');
    if (amountInfo) {
        amountInfo.textContent = '定金金额: ' + depositAmount.toFixed(2) + ' 元';
    }

    /* 优先使用后端返回的 deposit_qrcode_paths（0元预设的二维码），回退到班级0元价位 */
    var qrcodePaths = window._depositQrcodePaths || [];
    if (qrcodePaths.length === 0 && window._classPrices) {
        for (var i = 0; i < window._classPrices.length; i++) {
            var p = window._classPrices[i];
            var pAmt = (typeof p.price === 'number') ? p.price : parseFloat(p.price);
            if (pAmt < 0.001 && p.qrcode_paths && p.qrcode_paths.length > 0) {
                qrcodePaths = p.qrcode_paths;
                break;
            }
        }
    }
    _renderQrcodeGallery('deposit-qrcode-list', 'deposit-qrcode-indicator', qrcodePaths);

    App.showModal('deposit-modal');
}

function cancelPayment() {
    _pendingRegBody = null;
    App.hideModal('qrcode-modal');
}

function confirmPayment() {
    if (!_pendingRegBody) { return; }

    var btn = document.getElementById('confirm-pay-btn');
    btn.disabled = true;
    btn.textContent = '提交中...';

    App.apiPost('/api/registration/register', _pendingRegBody).then(function(res) {
        btn.disabled = false;
        btn.textContent = '确认缴费';
        App.hideModal('qrcode-modal');
        var submittedBody = _pendingRegBody;
        _pendingRegBody = null;

        if (res.data.code === 0) {
            App.showToast('报名成功！', 'success', 3000);
            /* Reset form, restore single empty student row */
            document.getElementById('register-form').reset();
            _studentRowCounter = 0;
            _amountManuallyModified = false;
            var listEl = document.getElementById('student-list');
            if (listEl) { listEl.innerHTML = ''; }
            addStudentRow();
            var hintEl = document.getElementById('price-headcount-hint');
            if (hintEl) { hintEl.style.display = 'none'; }
            var periodGroup = document.getElementById('period-group');
            var amountGroup = document.getElementById('amount-group');
            if (periodGroup) { periodGroup.style.display = 'none'; }
            if (amountGroup) { amountGroup.style.display = 'none'; }
            var suggestedHint = document.getElementById('suggested-amount-hint');
            if (suggestedHint) { suggestedHint.textContent = ''; }
            onPayTypeChange();
            backToClassList();
            loadClasses();
        } else if (res.data.code === 2004) {
            /* session expired, redirect to login */
            App.showToast('登录已过期，请重新登录', 'error');
            setTimeout(function() { window.location.replace('/login'); }, 1500);
        } else if (res.data.code === 5004) {
            /* headcount mismatch (backend fallback) */
            var expected = submittedBody.students ? submittedBody.students.length : 0;
            showHeadcountMismatchModal(expected, expected);
        } else {
            _showRegError(res.data.message || '报名失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '确认缴费';
        App.hideModal('qrcode-modal');
        _pendingRegBody = null;
        _showRegError('网络错误，请重试');
    });
}

function cancelDeposit() {
    _pendingRegBody = null;
    App.hideModal('deposit-modal');
}

function confirmDeposit() {
    if (!_pendingRegBody) { return; }

    var btn = document.getElementById('confirm-deposit-btn');
    btn.disabled = true;
    btn.textContent = '提交中...';

    App.apiPost('/api/registration/register', _pendingRegBody).then(function(res) {
        btn.disabled = false;
        btn.textContent = '确认定金报名';
        App.hideModal('deposit-modal');
        _pendingRegBody = null;

        if (res.data.code === 0) {
            App.showToast('定金报名成功！', 'success', 3000);
            document.getElementById('register-form').reset();
            _studentRowCounter = 0;
            _amountManuallyModified = false;
            var listEl = document.getElementById('student-list');
            if (listEl) { listEl.innerHTML = ''; }
            addStudentRow();
            var hintEl = document.getElementById('price-headcount-hint');
            if (hintEl) { hintEl.style.display = 'none'; }
            var periodGroup = document.getElementById('period-group');
            var amountGroup = document.getElementById('amount-group');
            if (periodGroup) { periodGroup.style.display = 'none'; }
            if (amountGroup) { amountGroup.style.display = 'none'; }
            var suggestedHint = document.getElementById('suggested-amount-hint');
            if (suggestedHint) { suggestedHint.textContent = ''; }
            onPayTypeChange();
            backToClassList();
            loadClasses();
        } else if (res.data.code === 2004) {
            App.showToast('登录已过期，请重新登录', 'error');
            setTimeout(function() { window.location.replace('/login'); }, 1500);
        } else if (res.data.code === 5009) {
            /* ERR_ZERO_PRESET_NOT_FOUND */
            _showRegError('请先创建价格为 0 的价格预设');
            App.showToast('请先创建价格为 0 的价格预设', 'error', 3000);
            setTimeout(function() { App.navigateTo('/preset'); }, 1500);
        } else if (res.data.code === 5008) {
            /* ERR_DEPOSIT_AMOUNT_INVALID */
            _showRegError('请输入有效的定金金额');
        } else {
            _showRegError(res.data.message || '定金报名失败');
        }
    }).catch(function() {
        btn.disabled = false;
        btn.textContent = '确认定金报名';
        App.hideModal('deposit-modal');
        _pendingRegBody = null;
        _showRegError('网络错误，请重试');
    });
}

function _showRegError(msg) {
    var errEl = document.getElementById('reg-error');
    if (errEl) {
        errEl.textContent = msg;
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
