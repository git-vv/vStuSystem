/* class_manage.js - Class management page logic */

var currentClassId = 0;
var currentClassName = '';
var currentClassStartTime = '';
var currentClassEndTime = '';
var currentUserRole = -1;
var currentStudents = [];
var studentQueryData = [];
var studentQueryType = 'registration';

(function() {
    App.requireLogin(function(session) {
        currentUserRole = session.role;
        var nameEl = document.getElementById('user-name');
        if (nameEl) { nameEl.textContent = session.username; }
        loadClassList();

        /* Set today's date for attendance */
        var dateInput = document.getElementById('attendance-date');
        if (dateInput) {
            var today = new Date();
            var yyyy = today.getFullYear();
            var mm = String(today.getMonth() + 1).padStart(2, '0');
            var dd = String(today.getDate()).padStart(2, '0');
            dateInput.value = yyyy + '-' + mm + '-' + dd;
        }

        /* Search on enter */
        var keywordInput = document.getElementById('class-keyword');
        if (keywordInput) {
            keywordInput.addEventListener('keyup', function(e) {
                if (e.key === 'Enter') { loadClassList(); }
            });
        }

        /* Resource select change triggers allocation list reload */
        var resourceSelect = document.getElementById('alloc-resource-id');
        if (resourceSelect) {
            resourceSelect.addEventListener('change', function() {
                loadAllocations();
                populateAllocStudents();
            });
        }

        /* Renew modal: recalculate suggested amount when end date changes */
        var renewNewEnd = document.getElementById('renew-new-end');
        if (renewNewEnd) {
            renewNewEnd.addEventListener('change', function() {
                calculateSuggestedRenewAmount();
            });
        }

        /* Renew modal: track manual amount modification */
        var renewAmountEl = document.getElementById('renew-amount');
        if (renewAmountEl) {
            renewAmountEl.addEventListener('input', function() {
                _renewAmountManuallyModified = true;
            });
        }
    });
})();

function handleLogout() {
    App.apiPost('/api/auth/logout', {}).then(function() {
        window.location.replace('/');
    });
}

/* --- Class list --- */
function loadClassList() {
    var container = document.getElementById('class-list');
    if (!container) { return; }
    container.innerHTML = '<div class="loading">加载中...</div>';

    var filter = document.getElementById('class-filter').value;
    var keyword = (document.getElementById('class-keyword').value || '').trim();
    var url = '/api/class/list?filter=' + filter;
    if (keyword) { url += '&keyword=' + encodeURIComponent(keyword); }

    App.apiGet(url).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var classes = res.data.data.classes || [];
        if (classes.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128218;</div><div class="empty-text">暂无班级</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < classes.length; i++) {
            var c = classes[i];
            var used = parseFloat(c.enrollment_used) || 0;
            used = Math.round(used * 100) / 100;
            var remain = c.enrollment_capacity - used;
            var usedStr = (used === Math.floor(used)) ? String(Math.floor(used)) : used.toFixed(2);
            var statusBadge = remain > 0.001
                ? '<span class="item-badge badge-green">可报名</span>'
                : '<span class="item-badge badge-red">已满</span>';

            html += '<div class="list-item" onclick="selectClass(' + c.id + ')">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(c.class_name) + statusBadge + '</div>';
            html += '    <div class="item-sub">';
            html += '      ' + _esc(c.class_type) + ' | ' + App.formatDate(c.start_time) + ' ~ ' + App.formatDate(c.end_time);
            html += '      | ' + usedStr + '/' + c.enrollment_capacity;
            html += '    </div>';
            html += '  </div>';
            if (currentUserRole === 0) {
                html += '  <div class="item-actions">';
                html += '    <button class="btn btn-danger btn-sm" onclick="event.stopPropagation(); deleteClass(' + c.id + ', \'' + _esc(c.class_name) + '\')">删除</button>';
                html += '  </div>';
            }
            html += '</div>';
        }
        container.innerHTML = html;
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

function deleteClass(classId, className) {
    if (!confirm('确定要删除班级 "' + className + '" 吗？删除后该班级的所有报名记录和考勤记录也将被删除！')) { return; }

    App.apiPost('/api/class/delete', { id: classId }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('班级已删除', 'success');
            if (currentClassId === classId) {
                backToClassList();
            }
            loadClassList();
        } else {
            App.showToast(res.data.message || '删除失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Select class --- */
function selectClass(classId) {
    currentClassId = classId;

    App.apiGet('/api/class/detail?id=' + classId).then(function(res) {
        if (res.data.code !== 0) {
            App.showToast(res.data.message || '获取详情失败', 'error');
            return;
        }

        var data = res.data.data;
        currentClassName = data.class_name || '';
        currentClassStartTime = data.start_time || '';
        currentClassEndTime = data.end_time || '';

        /* Show detail */
        document.getElementById('detail-class-name').textContent = data.class_name;
        var infoEl = document.getElementById('detail-info');
        if (infoEl) {
            var used = parseFloat(data.enrollment_used) || 0;
            used = Math.round(used * 100) / 100;
            var remain = data.enrollment_capacity - used;
            var usedStr = (used === Math.floor(used)) ? String(Math.floor(used)) : used.toFixed(2);
            var html = '<div style="font-size:0.9rem; color:#4a5568; line-height:1.8;">';
            html += '<div>类型: ' + _esc(data.class_type) + '</div>';
            html += '<div>时间: ' + App.formatDate(data.start_time) + ' ~ ' + App.formatDate(data.end_time) + '</div>';
            html += '<div>名额: ' + usedStr + ' / ' + data.enrollment_capacity;
            if (remain <= 0) { html += ' <span style="color:#e53e3e;">(已满)</span>'; }
            html += '</div>';
            if (data.description) { html += '<div>描述: ' + _esc(data.description) + '</div>'; }
            html += '</div>';
            infoEl.innerHTML = html;
        }

        /* Enrollment edit (admin only) */
        var enrollEdit = document.getElementById('enrollment-edit');
        if (enrollEdit) {
            if (currentUserRole === 0) {
                enrollEdit.style.display = 'block';
                document.getElementById('edit-capacity').value = data.enrollment_capacity;
            } else {
                enrollEdit.style.display = 'none';
            }
        }

        /* Price edit entry (admin only) */
        var priceEntry = document.getElementById('prices-edit-entry');
        if (priceEntry) {
            priceEntry.style.display = (currentUserRole === 0) ? 'block' : 'none';
        }

        /* Switch view */
        document.getElementById('section-class-list').style.display = 'none';
        document.getElementById('section-class-detail').style.display = 'block';
        window.scrollTo(0, 0);

        /* Load sub-tabs */
        loadStudents();
        loadResources();
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function backToClassList() {
    document.getElementById('section-class-list').style.display = 'block';
    document.getElementById('section-class-detail').style.display = 'none';
    var querySection = document.getElementById('section-student-query');
    if (querySection) { querySection.style.display = 'none'; }
    currentClassId = 0;
    currentClassName = '';
    currentClassStartTime = '';
    currentClassEndTime = '';
    window.scrollTo(0, 0);
}

/* --- Tab switching --- */
function switchManageTab(tab) {
    var tabs = ['students', 'attendance', 'resource'];
    for (var i = 0; i < tabs.length; i++) {
        var btn = document.getElementById('tab-' + tabs[i]);
        var content = document.getElementById('content-' + tabs[i]);
        if (btn && content) {
            if (tabs[i] === tab) {
                btn.classList.add('active');
                content.classList.add('active');
            } else {
                btn.classList.remove('active');
                content.classList.remove('active');
            }
        }
    }
    if (tab === 'attendance') { loadAttendance(); }
    if (tab === 'resource') { loadResources(); loadAllocations(); }
}

/* --- Update enrollment --- */
function updateEnrollment() {
    var capacity = parseInt(document.getElementById('edit-capacity').value, 10);
    if (!capacity || capacity <= 0) {
        App.showToast('名额必须大于0', 'error');
        return;
    }

    App.apiPut('/api/class/enrollment', {
        class_id: currentClassId,
        capacity: capacity
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('修改成功', 'success');
            selectClass(currentClassId);
        } else {
            App.showToast(res.data.message || '修改失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Students --- */
function loadStudents() {
    var container = document.getElementById('student-list');
    if (!container) { return; }
    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/class/students?class_id=' + currentClassId).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        currentStudents = res.data.data.students || [];
        if (currentStudents.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128100;</div><div class="empty-text">暂无报名学生</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < currentStudents.length; i++) {
            var s = currentStudents[i];
            var genderBadge = s.student_gender === 'male'
                ? '<span class="item-badge badge-blue">男</span>'
                : '<span class="item-badge badge-purple">女</span>';
            var bedTag = '';
            if (s.need_bed) {
                var bedCode = parseInt(s.bed_resource_code, 10);
                if (bedCode > 0) {
                    bedTag = '<span class="tag tag-green">床位' + bedCode + '号</span>';
                } else {
                    bedTag = '<span class="tag tag-orange">需床位</span>';
                }
            }
            var allergyTag = s.has_allergy ? '<span class="tag tag-red">过敏</span>' : '';

            /* 退费状态标签：管理员可见，老师可见 tag 但不能操作 */
            var refundNum = parseFloat(s.refund_amount) || 0;
            var refundTag = '';
            if (s.fully_refunded) {
                refundTag = '<span class="tag tag-refund">已全额退费</span>';
            } else if (refundNum > 0.001) {
                refundTag = '<span class="tag tag-refund">已退 ' + refundNum.toFixed(2) + '</span>';
            }
            /* 定金状态标签 */
            var depositTag = '';
            if (s.is_deposit === 1) {
                var depositAmt = parseFloat(s.paid_amount_snapshot) || 0;
                depositTag = '<span class="tag tag-blue">定金' + depositAmt.toFixed(1) + '元</span>';
            }
            /* 部分时段标签 */
            var periodTag = '';
            if (s.is_partial_period) {
                var pStart = s.student_start_date || '';
                var pEnd = s.student_end_date || '';
                if (pStart && pEnd) {
                    periodTag = '<span class="tag tag-gray">' + App.formatDate(pStart).slice(5) + '~' + App.formatDate(pEnd).slice(5) + '</span>';
                }
            }

            html += '<div class="list-item" data-student-id="' + s.id + '" onclick="showStudentDetail(' + s.id + ')">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(s.student_name) + genderBadge + '</div>';
            html += '    <div class="item-sub">';
            html += '      电话: ' + _esc(s.parent_phone);
            html += '      | 教师: ' + _esc(s.teacher_name);
            if (s.other_info) {
                html += '      | <span style="display:inline-block;max-width:8em;vertical-align:bottom;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;">' + _esc(s.other_info) + '</span>';
            }
            html += '    </div>';
            html += '    <div class="item-sub">' + bedTag + allergyTag + refundTag + depositTag + periodTag + '</div>';
            html += '  </div>';
            html += '  <div class="item-actions" style="margin-left:auto; align-self:center;">';
            html += '    <button class="btn btn-secondary btn-sm" style="min-width:44px;min-height:44px;" '
                 +  'onclick="event.stopPropagation();editStudent(' + s.id + ')">编辑</button>';
            /* 管理员可见删除按钮 */
            if (currentUserRole === 0) {
                html += '    <button class="btn btn-danger btn-sm" style="min-width:44px;min-height:44px;margin-left:6px;" '
                     +  'onclick="event.stopPropagation();deleteStudent(' + s.id + ',\'' + _esc(s.student_name).replace(/'/g, "\\'") + '\')">删除</button>';
            }
            /* 定金学生显示补缴按钮（教师权限可操作，已全额退费学生不显示） */
            if (s.is_deposit === 1 && !s.fully_refunded) {
                html += '    <button class="btn btn-primary btn-sm" style="min-width:44px;min-height:44px;margin-left:6px;" '
                     +  'onclick="event.stopPropagation();openSupplement(' + s.id + ',\'' + _esc(s.student_name).replace(/'/g, "\\'") + '\')">补缴</button>';
            }
            /* 管理员可见退费按钮：根据是否已退切换文案 */
            if (currentUserRole === 0) {
                if (refundNum > 0.001) {
                    html += '    <button class="btn btn-warning btn-sm" style="min-width:44px;min-height:44px;margin-left:6px;" '
                         +  'onclick="event.stopPropagation();cancelRefund(' + s.id + ',\'' + _esc(s.student_name).replace(/'/g, "\\'") + '\')">撤销退费</button>';
                } else {
                    html += '    <button class="btn btn-warning btn-sm" style="min-width:44px;min-height:44px;margin-left:6px;" '
                         +  'onclick="event.stopPropagation();openRefundModal(' + s.id + ',\'' + _esc(s.student_name).replace(/'/g, "\\'") + '\',' + (parseFloat(s.paid_amount) || 0).toFixed(2) + ')">退费</button>';
                }
            }
            html += '  </div>';
            html += '</div>';
        }
        container.innerHTML = html;

        /* Also populate allocation student list */
        populateAllocStudents();
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

/* --- Edit student --- */
function onAllergyChange() {
    var v = document.getElementById('edit-stu-allergy').value;
    var wrap = document.getElementById('edit-stu-allergy-desc-wrap');
    var desc = document.getElementById('edit-stu-allergy-desc');
    if (v === '1') {
        wrap.style.display = '';
        if (desc) { desc.focus(); }
    } else {
        wrap.style.display = 'none';
        if (desc) { desc.value = ''; }
    }
}

function editStudent(studentId) {
    if (!currentClassId) {
        App.showToast('请先选择班级', 'error');
        return;
    }

    Promise.all([
        App.apiGet('/api/class/student?class_id=' + currentClassId + '&student_id=' + studentId),
        App.apiGet('/api/class/list?filter=all'),
        App.apiGet('/api/class/detail?id=' + currentClassId)
    ]).then(function(results) {
        var stuResp = results[0].data;
        var classResp = results[1].data;
        var detailResp = results[2].data;

        if (stuResp.code !== 0) {
            App.showToast(stuResp.message || '获取学生信息失败', 'error');
            return;
        }
        if (classResp.code !== 0) {
            App.showToast(classResp.message || '获取班级列表失败', 'error');
            return;
        }
        if (detailResp.code !== 0) {
            App.showToast(detailResp.message || '获取班级详情失败', 'error');
            return;
        }

        var s = stuResp.data;
        var classes = classResp.data.classes || [];
        var prices = (detailResp.data && detailResp.data.prices) ? detailResp.data.prices : [];

        document.getElementById('edit-stu-id').value = s.id;
        document.getElementById('edit-stu-name').value = s.student_name || '';
        document.getElementById('edit-stu-gender').value = s.student_gender || 'male';
        document.getElementById('edit-stu-phone').value = s.parent_phone || '';
        document.getElementById('edit-stu-allergy').value = s.has_allergy ? '1' : '0';
        document.getElementById('edit-stu-allergy-desc').value = s.allergy_desc || '';
        document.getElementById('edit-stu-other-info').value = s.other_info || '';
        document.getElementById('edit-stu-teacher').value = s.teacher_name || '';

        /* 填充班级下拉 */
        var classSel = document.getElementById('edit-stu-class');
        classSel.innerHTML = '';
        for (var i = 0; i < classes.length; i++) {
            var opt = document.createElement('option');
            opt.value = classes[i].id;
            opt.textContent = classes[i].class_name;
            if (classes[i].id === s.class_id) { opt.selected = true; }
            classSel.appendChild(opt);
        }

        /* 匹配 price_id 显示为"<activity_name>/<price>(N人)" */
        var priceText = '';
        for (var j = 0; j < prices.length; j++) {
            if (prices[j].id === s.price_id) {
                var pAmt = (typeof prices[j].price === 'number') ? prices[j].price.toFixed(2) : prices[j].price;
                var pHc = prices[j].expected_headcount || 1;
                priceText = prices[j].activity_name + '/' + pAmt + '(' + pHc + '人)';
                break;
            }
        }
        document.getElementById('edit-stu-price').value = priceText;

        /* 触发过敏联动 */
        onAllergyChange();

        /* 填充上课时段 */
        var periodEl = document.getElementById('edit-stu-period');
        var stuStart = s.student_start_date || '';
        var stuEnd = s.student_end_date || '';
        var classStart = detailResp.data.start_time || '';
        var classEnd = detailResp.data.end_time || '';
        var displayStart = stuStart || classStart;
        var displayEnd = stuEnd || classEnd;
        if (periodEl) {
            periodEl.value = App.formatDate(displayStart) + ' ~ ' + App.formatDate(displayEnd);
        }

        /* 续费按钮：结束日期未到班级末尾且非全额退费时显示 */
        var btnRenew = document.getElementById('btn-renew');
        var canRenew = s.can_renew === true || s.can_renew === 1;
        var isFullyRefunded = s.fully_refunded === true || s.fully_refunded === 1;
        if (btnRenew) {
            btnRenew.style.display = (canRenew && !isFullyRefunded) ? 'inline-block' : 'none';
        }

        /* 存储续费所需的学生/班级数据 */
        window._renewContext = {
            regId: s.id,
            studentStart: stuStart || classStart,
            studentEnd: stuEnd || classEnd,
            classStart: classStart,
            classEnd: classEnd,
            priceId: s.price_id || 0,
            isPartial: s.is_partial_period === true || s.is_partial_period === 1
        };

        /* 定金学生显示补缴区，否则隐藏 */
        var supplementGroup = document.getElementById('supplement-group');
        if (supplementGroup) {
            supplementGroup.style.display = (s.is_deposit === 1) ? 'block' : 'none';
        }
        var supplementRegIdEl = document.getElementById('supplement-reg-id');
        if (supplementRegIdEl) { supplementRegIdEl.value = s.id; }

        App.showModal('student-edit-modal');
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function submitStudentEdit() {
    var id = document.getElementById('edit-stu-id').value;
    var name = document.getElementById('edit-stu-name').value.trim();
    var gender = document.getElementById('edit-stu-gender').value;
    var phone = document.getElementById('edit-stu-phone').value.trim();
    var allergy = document.getElementById('edit-stu-allergy').value;
    var allergyDesc = document.getElementById('edit-stu-allergy-desc').value.trim();
    var classId = document.getElementById('edit-stu-class').value;
    var otherInfo = document.getElementById('edit-stu-other-info').value;
    var teacher = document.getElementById('edit-stu-teacher').value;

    if (!name) { App.showToast('姓名不能为空', 'error'); return; }
    if (phone && !/^\d{7,15}$/.test(phone)) { App.showToast('家长电话格式不正确（7-15位数字）', 'error'); return; }
    if (allergy === '1' && !allergyDesc) { App.showToast('过敏描述不能为空', 'error'); return; }

    var body = {
        registration_id: parseInt(id, 10),
        student_name: name,
        student_gender: gender,
        parent_phone: phone,
        has_allergy: parseInt(allergy, 10),
        allergy_desc: allergy === '1' ? allergyDesc : '',
        class_id: parseInt(classId, 10),
        other_info: otherInfo,
        teacher_name: teacher
    };

    App.apiPost('/api/class/student/update', body).then(function(res) {
        if (res.data.code === 0) {
            App.hideModal('student-edit-modal');
            loadStudents();
            App.showToast('修改成功', 'success');
        } else {
            App.showToast(res.data.message || '修改失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Supplement (deposit -> full) --- */
function openSupplement(regId, studentName) {
    if (!currentClassId) {
        App.showToast('请先选择班级', 'error');
        return;
    }
    document.getElementById('supplement-reg-id').value = regId;
    /* 取班级预设列表（排除 0 元预设）填充下拉 */
    App.apiGet('/api/class/detail?id=' + currentClassId).then(function(res) {
        if (res.data.code !== 0) {
            App.showToast(res.data.message || '获取班级预设失败', 'error');
            return;
        }
        var prices = (res.data.data && res.data.data.prices) ? res.data.data.prices : [];
        var sel = document.getElementById('supplement-target-preset');
        sel.innerHTML = '<option value="">请选择目标价位</option>';
        window._supplementPrices = prices;
        for (var i = 0; i < prices.length; i++) {
            var p = prices[i];
            var pAmt = (typeof p.price === 'number') ? p.price : parseFloat(p.price);
            if (pAmt < 0.001) { continue; }  /* 排除 0 元预设 */
            var opt = document.createElement('option');
            opt.value = p.preset_id;
            opt.dataset.priceId = p.id;
            opt.dataset.amount = pAmt;
            var hc = p.expected_headcount || 1;
            opt.textContent = (p.activity_name || '默认') + ' - ' + pAmt.toFixed(2) + '(' + hc + '人)';
            sel.appendChild(opt);
        }
        /* 取该学生定金金额 */
        App.apiGet('/api/class/student?class_id=' + currentClassId + '&student_id=' + regId).then(function(stuRes) {
            if (stuRes.data.code === 0) {
                window._supplementDepositPaid = parseFloat(stuRes.data.data.paid_amount_snapshot) || 0;
            } else {
                window._supplementDepositPaid = 0;
            }
            document.getElementById('supplement-hint').textContent = '';
            App.showModal('student-edit-modal');
        });
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function onSupplementPresetChange() {
    var sel = document.getElementById('supplement-target-preset');
    var hintEl = document.getElementById('supplement-hint');
    if (!sel || !sel.value) { if (hintEl) { hintEl.textContent = ''; } return; }
    var selectedOpt = sel.options[sel.selectedIndex];
    var targetAmount = parseFloat(selectedOpt.dataset.amount) || 0;
    var depositPaid = parseFloat(window._supplementDepositPaid) || 0;
    var diff = targetAmount - depositPaid;
    if (hintEl) {
        hintEl.textContent = '补缴 ' + diff.toFixed(2) + ' 元，补缴后总付 ' + targetAmount.toFixed(2) + ' 元';
    }
}

function confirmSupplement() {
    var regId = parseInt(document.getElementById('supplement-reg-id').value, 10);
    var sel = document.getElementById('supplement-target-preset');
    if (!sel || !sel.value) {
        App.showToast('请选择目标价位', 'error');
        return;
    }
    var targetPresetId = parseInt(sel.value, 10);

    var body = {
        registration_id: regId,
        target_preset_id: targetPresetId
    };
    App.apiPost('/api/class/students/supplement', body).then(function(res) {
        if (res.data.code === 0) {
            App.hideModal('student-edit-modal');
            App.showToast('补缴成功，已转为全额', 'success');
            loadStudents();
        } else if (res.data.code === 5011) {
            /* ERR_SUPPLEMENT_ALREADY_DONE */
            App.showToast('该学生已补缴，不可重复补缴', 'error');
            App.hideModal('student-edit-modal');
            loadStudents();
        } else if (res.data.code === 5010) {
            /* ERR_SUPPLEMENT_AMOUNT_INVALID */
            App.showToast('补缴金额不符合班级要求', 'error');
        } else if (res.data.code === 5012) {
            /* ERR_SUPPLEMENT_PRESET_NOT_IN_CLASS */
            App.showToast('目标预设不属于该班级', 'error');
        } else {
            App.showToast(res.data.message || '补缴失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function showStudentDetail(studentId) {
    App.apiGet('/api/class/student?class_id=' + currentClassId + '&student_id=' + studentId).then(function(res) {
        if (res.data.code !== 0) {
            App.showToast(res.data.message || '获取详情失败', 'error');
            return;
        }

        var s = res.data.data;
        var html = '<div style="font-size:0.9rem; line-height:2;">';
        html += '<div><strong>姓名:</strong> ' + _esc(s.student_name) + '</div>';
        html += '<div><strong>性别:</strong> ' + App.genderLabel(s.student_gender) + '</div>';
        html += '<div><strong>家长电话:</strong> ' + _esc(s.parent_phone) + '</div>';
        html += '<div><strong>过敏:</strong> ' + (s.has_allergy ? '是 - ' + _esc(s.allergy_desc) : '否') + '</div>';
        var bedLabel = '否';
        if (s.need_bed) {
            var bedCode = parseInt(s.bed_resource_code, 10);
            bedLabel = bedCode > 0 ? '是 - 床位' + bedCode + '号' : '是';
        }
        html += '<div><strong>需要床位:</strong> ' + bedLabel + '</div>';
        html += '<div><strong>负责教师:</strong> ' + _esc(s.teacher_name) + '</div>';
        /* 上课时段 */
        var stuStart = s.student_start_date || '';
        var stuEnd = s.student_end_date || '';
        if (stuStart && stuEnd) {
            html += '<div><strong>上课时段:</strong> ' + App.formatDate(stuStart) + ' ~ ' + App.formatDate(stuEnd) + '</div>';
        }
        html += '<div><strong>报名时间:</strong> ' + _esc(s.register_time) + '</div>';
        html += '<div><strong>备注:</strong> ' + (s.other_info ? _esc(s.other_info) : '') + '</div>';
        html += '</div>';

        document.getElementById('student-detail').innerHTML = html;
        App.showModal('student-modal');
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Attendance --- */
function loadAttendance() {
    var container = document.getElementById('attendance-list');
    if (!container) { return; }

    var date = document.getElementById('attendance-date').value;
    if (!date) {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">请选择日期</div></div>';
        return;
    }

    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/class/attendance/query?class_id=' + currentClassId + '&date=' + date).then(function(res) {
        var existing = {};
        if (res.data.code === 0) {
            var records = (res.data.data && res.data.data.records) ? res.data.data.records : [];
            for (var i = 0; i < records.length; i++) {
                existing[records[i].registration_id] = records[i];
            }
        }
        renderAttendanceForm(container, existing);
    }).catch(function() {
        renderAttendanceForm(container, {});
    });
}

function renderAttendanceForm(container, existing) {
    if (currentStudents.length === 0) {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">暂无学生，无法提交考勤</div></div>';
        return;
    }

    var attDate = document.getElementById('attendance-date').value || '';

    var html = '<div class="attendance-grid">';
    for (var i = 0; i < currentStudents.length; i++) {
        var s = currentStudents[i];
        /* 已全额退费学生不参与考勤 */
        if (s.fully_refunded) { continue; }
        /* 部分时段学生：考勤日期超出上课时段则不渲染 */
        if (attDate && s.student_start_date && s.student_end_date) {
            if (attDate < s.student_start_date || attDate > s.student_end_date) { continue; }
        }
        var ex = existing[s.id] || {};
        var status = (ex.status !== undefined) ? ex.status : 0;
        var leaveTime = ex.leave_time || '';
        var statusClass0 = status === 0 ? ' present-active' : '';
        var statusClass1 = status === 1 ? ' absent-active' : '';
        var statusClass2 = status === 2 ? ' early-active' : '';
        var statusClass3 = status === 3 ? ' late-active' : '';
        html += '<div class="attendance-row" data-reg-id="' + s.id + '" data-name="' + _esc(s.student_name) + '" data-gender="' + _esc(s.student_gender) + '" data-status="' + status + '" data-leave-time="' + _esc(leaveTime) + '">';
        html += '  <div class="student-name">' + _esc(s.student_name) + '</div>';
        html += '  <div class="status-btns">';
        html += '    <button type="button" class="status-btn' + statusClass0 + '" onclick="setAttendanceStatus(this, 0)">出勤</button>';
        html += '    <button type="button" class="status-btn' + statusClass1 + '" onclick="setAttendanceStatus(this, 1)">缺勤</button>';
        html += '    <button type="button" class="status-btn' + statusClass2 + '" onclick="openTimeModal(this, 2)">早退</button>';
        html += '    <button type="button" class="status-btn' + statusClass3 + '" onclick="openTimeModal(this, 3)">迟到</button>';
        html += '  </div>';
        html += '</div>';
    }
    html += '</div>';
    container.innerHTML = html;
}

function setAttendanceStatus(btn, status) {
    var row = btn.closest('.attendance-row');
    if (!row) { return; }
    row.setAttribute('data-status', status);
    row.setAttribute('data-leave-time', '');

    var btns = row.querySelectorAll('.status-btn');
    btns[0].className = 'status-btn' + (status === 0 ? ' present-active' : '');
    btns[1].className = 'status-btn' + (status === 1 ? ' absent-active' : '');
    btns[2].className = 'status-btn' + (status === 2 ? ' early-active' : '');
    btns[3].className = 'status-btn' + (status === 3 ? ' late-active' : '');

    if (status === 0 || status === 1) {
        submitAttendance(parseInt(row.getAttribute('data-reg-id'), 10));
    }
}

var timeModalTargetRow = null;
var timeModalTargetStatus = 2; /* 2=EarlyLeave, 3=Late */
var earlyLeaveSelectsPopulated = false;

function populateEarlyLeaveSelects() {
    if (earlyLeaveSelectsPopulated) { return; }
    var hourSel = document.getElementById('early-leave-hour');
    var minuteSel = document.getElementById('early-leave-minute');
    if (!hourSel || !minuteSel) { return; }
    var i;
    for (i = 0; i < 24; i++) {
        var h = i < 10 ? '0' + i : '' + i;
        var opt = document.createElement('option');
        opt.value = h;
        opt.textContent = h;
        hourSel.appendChild(opt);
    }
    for (i = 0; i < 60; i++) {
        var m = i < 10 ? '0' + i : '' + i;
        var opt2 = document.createElement('option');
        opt2.value = m;
        opt2.textContent = m;
        minuteSel.appendChild(opt2);
    }
    earlyLeaveSelectsPopulated = true;
}

function openTimeModal(btn, status) {
    timeModalTargetRow = btn.closest('.attendance-row');
    if (!timeModalTargetRow) { return; }
    timeModalTargetStatus = status;
    populateEarlyLeaveSelects();
    var nameEl = document.getElementById('early-leave-student-name');
    if (nameEl) {
        nameEl.textContent = '学生：' + timeModalTargetRow.getAttribute('data-name');
    }
    var titleEl = document.getElementById('time-modal-title');
    if (titleEl) {
        titleEl.textContent = (status === 3) ? '选择迟到时间' : '选择早退时间';
    }
    var existingLeave = timeModalTargetRow.getAttribute('data-leave-time') || '';
    var hourSel = document.getElementById('early-leave-hour');
    var minuteSel = document.getElementById('early-leave-minute');
    if (existingLeave && existingLeave.length === 5) {
        hourSel.value = existingLeave.substr(0, 2);
        minuteSel.value = existingLeave.substr(3, 2);
    } else {
        hourSel.value = (status === 3) ? '09' : '14';
        minuteSel.value = '00';
    }
    App.showModal('early-leave-modal');
}

function confirmTimeModal() {
    if (!timeModalTargetRow) { return; }
    var hour = document.getElementById('early-leave-hour').value;
    var minute = document.getElementById('early-leave-minute').value;
    var leaveTime = hour + ':' + minute;
    var statusStr = String(timeModalTargetStatus);

    timeModalTargetRow.setAttribute('data-status', statusStr);
    timeModalTargetRow.setAttribute('data-leave-time', leaveTime);
    var btns = timeModalTargetRow.querySelectorAll('.status-btn');
    btns[0].className = 'status-btn';
    btns[1].className = 'status-btn';
    btns[2].className = 'status-btn' + (timeModalTargetStatus === 2 ? ' early-active' : '');
    btns[3].className = 'status-btn' + (timeModalTargetStatus === 3 ? ' late-active' : '');

    App.hideModal('early-leave-modal');
    var regId = parseInt(timeModalTargetRow.getAttribute('data-reg-id'), 10);
    timeModalTargetRow = null;
    submitAttendance(regId);
}

function submitAttendance(singleRegId) {
    var date = document.getElementById('attendance-date').value;
    if (!date) {
        App.showToast('请选择日期', 'error');
        return;
    }

    var rows = document.querySelectorAll('.attendance-row');
    if (rows.length === 0) {
        App.showToast('没有可提交的考勤记录', 'error');
        return;
    }

    var records = [];
    for (var i = 0; i < rows.length; i++) {
        var row = rows[i];
        if (singleRegId) {
            var rowRegId = parseInt(row.getAttribute('data-reg-id'), 10);
            if (rowRegId !== singleRegId) { continue; }
        }
        var status = parseInt(row.getAttribute('data-status') || '0', 10);
        var rec = {
            registration_id: parseInt(row.getAttribute('data-reg-id'), 10),
            student_name: row.getAttribute('data-name'),
            student_gender: row.getAttribute('data-gender'),
            status: status
        };
        if (status === 2 || status === 3) {
            rec.leave_time = row.getAttribute('data-leave-time') || '';
        }
        records.push(rec);
    }

    if (records.length === 0) { return; }

    App.apiPost('/api/class/attendance', {
        class_id: currentClassId,
        date: date,
        records: records
    }).then(function(res) {
        if (res.data.code === 0) {
            if (singleRegId) {
                App.showToast('考勤已更新', 'success');
            } else {
                App.showToast('考勤提交成功', 'success');
            }
            loadAttendance();
        } else {
            App.showToast(res.data.message || '提交失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Resource allocation --- */
function loadResources() {
    App.apiGet('/api/resource/list').then(function(res) {
        var select = document.getElementById('alloc-resource-id');
        if (!select) { return; }

        select.innerHTML = '<option value="">请选择资源</option>';
        if (res.data.code !== 0) { return; }

        var list = (res.data.data && res.data.data.list) ? res.data.data.list : [];
        /* Store resource list for bed check */
        window._resourceList = list;
        for (var i = 0; i < list.length; i++) {
            var r = list[i];
            var typeLabel = r.resource_type === 1 ? '[床位] ' : '';
            var opt = document.createElement('option');
            opt.value = r.id;
            opt.setAttribute('data-resource-type', r.resource_type || 0);
            opt.textContent = typeLabel + r.name + ' (剩余: ' + r.remain_count + ')';
            select.appendChild(opt);
        }

        /* 默认选中第一个资源并加载分配列表 */
        if (list.length > 0) {
            select.value = list[0].id;
            loadAllocations();
        }
    });
}

function populateAllocStudents() {
    var container = document.getElementById('alloc-student-list');
    if (!container) { return; }
    container.innerHTML = '';

    if (currentStudents.length === 0) { return; }

    /* Check if current resource is bed type */
    var resourceSelect = document.getElementById('alloc-resource-id');
    var isBedResource = false;
    if (resourceSelect && resourceSelect.selectedIndex > 0) {
        var selectedOpt = resourceSelect.options[resourceSelect.selectedIndex];
        isBedResource = (selectedOpt.getAttribute('data-resource-type') === '1');
    }

    var html = '<div class="form-group"><label>选择学生</label><select class="form-control" id="alloc-student-select">';
    html += '<option value="">请选择学生</option>';
    for (var i = 0; i < currentStudents.length; i++) {
        var s = currentStudents[i];
        /* If bed resource, only show students who need bed */
        if (isBedResource && !s.need_bed) { continue; }
        html += '<option value="' + s.id + '" data-name="' + _esc(s.student_name) + '" data-gender="' + _esc(s.student_gender) + '" data-need-bed="' + (s.need_bed ? 1 : 0) + '">' + _esc(s.student_name) + '</option>';
    }
    html += '</select></div>';
    if (isBedResource) {
        html += '<div style="color:#e53e3e; font-size:0.85rem; margin-top:4px;">仅显示报名时选择需要床位的学生</div>';
    }
    container.innerHTML = html;
}

function allocateResource() {
    var errEl = document.getElementById('alloc-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var resourceSelect = document.getElementById('alloc-resource-id');
    var resourceId = resourceSelect ? resourceSelect.value : '';
    var resourceCode = document.getElementById('alloc-resource-code').value;
    var studentSelect = document.getElementById('alloc-student-select');
    var studentId = studentSelect ? studentSelect.value : '';
    var studentName = '';
    var studentGender = '';
    var studentNeedBed = 0;

    if (studentSelect && studentSelect.selectedIndex > 0) {
        var opt = studentSelect.options[studentSelect.selectedIndex];
        studentName = opt.getAttribute('data-name') || '';
        studentGender = opt.getAttribute('data-gender') || '';
        studentNeedBed = parseInt(opt.getAttribute('data-need-bed') || '0', 10);
    }

    if (!resourceId) { _showAllocError('请选择资源'); return; }
    if (!resourceCode) { _showAllocError('请输入资源编号'); return; }

    /* Frontend check: bed resource requires student need_bed=1 */
    if (resourceSelect && resourceSelect.selectedIndex > 0) {
        var selectedOpt = resourceSelect.options[resourceSelect.selectedIndex];
        if (selectedOpt.getAttribute('data-resource-type') === '1' && studentNeedBed !== 1) {
            _showAllocError('该学生报名时未选择需要床位，不能分配床位资源');
            App.showToast('该学生报名时未选择需要床位，不能分配床位资源', 'error', 3000);
            return;
        }
    }

    App.apiPost('/api/class/allocate-resource', {
        class_id: currentClassId,
        resource_id: parseInt(resourceId, 10) || 0,
        resource_code: parseInt(resourceCode, 10) || 0,
        registration_id: studentId ? parseInt(studentId, 10) || 0 : 0,
        student_name: studentName,
        student_gender: studentGender,
        /* teacher_name omitted: backend fills it from current session username */
        class_name: currentClassName
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('分配成功', 'success');
            document.getElementById('alloc-resource-code').value = '';
            loadResources();
            loadAllocations();
            loadStudents();
        } else {
            var msg = res.data.message || '分配失败';
            _showAllocError(msg);
            App.showToast(msg, 'error', 3000);
        }
    }).catch(function() {
        _showAllocError('网络错误，请重试');
        App.showToast('网络错误，请重试', 'error');
    });
}

function loadAllocations() {
    var resourceId = document.getElementById('alloc-resource-id').value;
    var container = document.getElementById('alloc-list');
    if (!container || !resourceId) {
        if (container) { container.innerHTML = '<div class="empty-state"><div class="empty-text">请先选择资源查看分配记录</div></div>'; }
        return;
    }

    App.apiGet('/api/class/allocations?class_id=' + currentClassId + '&resource_id=' + resourceId).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var data = res.data.data || {};
        var allocs = data.allocations || [];
        if (allocs.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">暂无分配记录</div></div>';
            return;
        }

        var html = '';
        for (var i = 0; i < allocs.length; i++) {
            var a = allocs[i];
            html += '<div class="list-item">';
            html += '  <div class="item-main">';
            html += '    <div class="item-title">' + _esc(a.student_name) + ' <span class="tag tag-blue">编号' + a.resource_code + '</span></div>';
            html += '    <div class="item-sub">教师: ' + _esc(a.teacher_name) + '</div>';
            html += '  </div>';
            html += '</div>';
        }
        container.innerHTML = html;
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

function _showAllocError(msg) {
    var errEl = document.getElementById('alloc-error');
    if (errEl) {
        errEl.textContent = msg;
        errEl.classList.add('show');
    }
}

/* --- Price edit (admin only) --- */
var g_priceEditPresets = [];
var g_priceEditCounter = 0;

function _loadPricePresetsForEdit() {
    return App.apiGet('/api/class/price-presets').then(function(res) {
        if (res.data.code === 0) {
            g_priceEditPresets = res.data.data.presets || [];
        } else {
            g_priceEditPresets = [];
        }
    }).catch(function() {
        g_priceEditPresets = [];
    });
}

function _refillPriceEditSelect(sel, keepValue) {
    if (!sel) { return; }
    var cur = keepValue || sel.value || '';
    var html = '<option value="">请选择价位</option>';
    for (var i = 0; i < g_priceEditPresets.length; i++) {
        var p = g_priceEditPresets[i];
        var amt = (typeof p.amount === 'number') ? p.amount.toFixed(2) : p.amount;
        var pAmt = (typeof p.amount === 'number') ? p.amount : parseFloat(p.amount);
        if (pAmt < 0.001) { continue; } /* 0元预设为定金报名专用，不作为普通价位 */
        var hc = p.expected_headcount || 1;
        html += '<option value="' + p.id + '">' + _esc(amt) + '(' + hc + '人)</option>';
    }
    sel.innerHTML = html;
    if (cur) { sel.value = cur; }
}

function showPriceEditModal() {
    if (!currentClassId) {
        App.showToast('请先选择班级', 'error');
        return;
    }
    var errEl = document.getElementById('price-edit-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    _loadPricePresetsForEdit().then(function() {
        return App.apiGet('/api/class/detail?id=' + currentClassId);
    }).then(function(res) {
        var listEl = document.getElementById('price-edit-list');
        listEl.innerHTML = '';
        g_priceEditCounter = 0;

        if (res.data.code !== 0) {
            App.showToast(res.data.message || '获取班级价位失败', 'error');
            return;
        }

        var prices = (res.data.data && res.data.data.prices) ? res.data.data.prices : [];
        for (var i = 0; i < prices.length; i++) {
            _appendPriceEditItem(prices[i]);
        }
        App.showModal('price-edit-modal');
    });
}

function _appendPriceEditItem(priceInfo) {
    g_priceEditCounter++;
    var id = g_priceEditCounter;
    var listEl = document.getElementById('price-edit-list');

    var div = document.createElement('div');
    div.className = 'price-edit-item';
    div.id = 'price-edit-item-' + id;
    div.setAttribute('data-price-id', priceInfo ? (priceInfo.id || 0) : 0);
    div.setAttribute('data-orig-preset-id', priceInfo ? (priceInfo.preset_id || 0) : 0);

    var activityName = priceInfo ? (priceInfo.activity_name || '') : '';
    var presetId = priceInfo ? (priceInfo.preset_id || 0) : 0;
    var amt = priceInfo ? ((typeof priceInfo.price === 'number') ? priceInfo.price.toFixed(2) : priceInfo.price) : '';
    var hc = priceInfo ? (priceInfo.expected_headcount || 1) : 1;

    if (priceInfo && presetId > 0) {
        /* 已存在项：preset 不可更改，显示为只读文本 */
        div.innerHTML =
            '<button type="button" class="price-remove" onclick="removePriceEditItem(' + id + ')">&times;</button>' +
            '<div class="form-row">' +
            '  <div class="form-group">' +
            '    <label>活动名称</label>' +
            '    <input type="text" class="form-control price-edit-activity" value="' + _esc(activityName) + '">' +
            '  </div>' +
            '  <div class="form-group">' +
            '    <label>价位预设（不可更改）</label>' +
            '    <input type="text" class="form-control" value="' + _esc(amt) + '(' + hc + '人)" readonly>' +
            '    <input type="hidden" class="price-edit-preset" value="' + presetId + '">' +
            '  </div>' +
            '</div>';
    } else {
        /* 新增项：可选预设 */
        div.innerHTML =
            '<button type="button" class="price-remove" onclick="removePriceEditItem(' + id + ')">&times;</button>' +
            '<div class="form-row">' +
            '  <div class="form-group">' +
            '    <label>活动名称</label>' +
            '    <input type="text" class="form-control price-edit-activity" placeholder="如：全程班">' +
            '  </div>' +
            '  <div class="form-group">' +
            '    <label>价位预设</label>' +
            '    <select class="form-control price-edit-preset"><option value="">请选择价位</option></select>' +
            '  </div>' +
            '</div>';
        var sel = div.querySelector('.price-edit-preset');
        _refillPriceEditSelect(sel, '');
    }
    listEl.appendChild(div);
}

function addPriceEditItem() {
    _appendPriceEditItem(null);
}

function removePriceEditItem(id) {
    var el = document.getElementById('price-edit-item-' + id);
    if (el) { el.remove(); }
}

function submitPriceEdit() {
    var errEl = document.getElementById('price-edit-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    var items = document.querySelectorAll('.price-edit-item');
    if (items.length === 0) {
        _showPriceEditError('请至少保留一个价位项');
        return;
    }

    var prices = [];
    var seenPreset = {};
    for (var i = 0; i < items.length; i++) {
        var it = items[i];
        var activityName = it.querySelector('.price-edit-activity').value.trim();
        var presetId = parseInt(it.querySelector('.price-edit-preset').value, 10);
        if (!activityName) {
            _showPriceEditError('请填写第 ' + (i + 1) + ' 个价位项的活动名称');
            return;
        }
        if (!presetId || isNaN(presetId)) {
            _showPriceEditError('请选择第 ' + (i + 1) + ' 个价位项的价位预设');
            return;
        }
        if (seenPreset[presetId]) {
            _showPriceEditError('同一班级不能选择重复的价位预设');
            return;
        }
        seenPreset[presetId] = true;
        prices.push({
            price_id: parseInt(it.getAttribute('data-price-id'), 10) || 0,
            activity_name: activityName,
            preset_id: presetId
        });
    }

    App.apiPost('/api/class/prices/update', {
        class_id: currentClassId,
        prices: prices
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('价位修改成功', 'success');
            App.hideModal('price-edit-modal');
            selectClass(currentClassId);
        } else {
            _showPriceEditError(res.data.message || '修改失败');
        }
    }).catch(function() {
        _showPriceEditError('网络错误，请重试');
    });
}

function _showPriceEditError(msg) {
    var errEl = document.getElementById('price-edit-error');
    if (errEl) {
        errEl.textContent = msg;
        errEl.classList.add('show');
    }
}

/* --- Attendance print --- */
var attendancePrintMonths = [];
var attendancePrintCurrentPage = 0;
var attendancePrintData = {};

function showAttendancePrint() {
    if (!currentClassId) {
        App.showToast('请先选择班级', 'error');
        return;
    }
    if (!currentClassStartTime || !currentClassEndTime) {
        App.showToast('班级时间信息缺失', 'error');
        return;
    }

    var container = document.getElementById('attendance-print-area');
    if (!container) { return; }
    container.style.display = 'block';
    container.innerHTML = '<div id="attendance-print-content"></div>';

    attendancePrintMonths = buildMonthList(currentClassStartTime, currentClassEndTime);
    if (attendancePrintMonths.length === 0) {
        App.showToast('无法解析班级时间', 'error');
        return;
    }
    attendancePrintCurrentPage = 0;
    renderAttendancePrintPage();
}

function generateAttendancePrint() {
    var startInput = document.getElementById('att-print-start');
    var endInput = document.getElementById('att-print-end');
    if (!startInput || !endInput) { return; }

    var selStart = startInput.value;
    var selEnd = endInput.value;

    if (!selStart || !selEnd) {
        App.showToast('请选择起止日期', 'error');
        return;
    }
    if (selStart > selEnd) {
        App.showToast('起始日期不能晚于结束日期', 'error');
        return;
    }
    if (selStart < currentClassStartTime || selEnd > currentClassEndTime) {
        App.showToast('日期范围必须在班级时间跨度内', 'error');
        return;
    }

    attendancePrintMonths = buildMonthList(selStart, selEnd);
    if (attendancePrintMonths.length === 0) {
        App.showToast('无法解析时间范围', 'error');
        return;
    }

    attendancePrintCurrentPage = 0;

    var content = document.getElementById('attendance-print-content');
    if (!content) { return; }
    content.innerHTML = '<div class="loading">加载中...</div>';

    renderAttendancePrintPage();
}

function buildMonthList(startDate, endDate) {
    /* startDate/endDate 格式 YYYY-MM-DD */
    var months = [];
    var start = new Date(startDate + 'T00:00:00');
    var end = new Date(endDate + 'T00:00:00');
    if (isNaN(start.getTime()) || isNaN(end.getTime())) {
        return months;
    }

    var cur = new Date(start.getFullYear(), start.getMonth(), 1);
    var endMonth = new Date(end.getFullYear(), end.getMonth(), 1);
    while (cur <= endMonth) {
        var y = cur.getFullYear();
        var m = cur.getMonth();
        var monthStart = new Date(y, m, 1);
        var monthEnd = new Date(y, m + 1, 0);
        /* 截取到班级时间范围内 */
        var rangeStart = monthStart < start ? start : monthStart;
        var rangeEnd = monthEnd > end ? end : monthEnd;
        months.push({
            year: y,
            month: m,
            start: formatDateStr(rangeStart),
            end: formatDateStr(rangeEnd),
            label: y + '年' + (m + 1) + '月'
        });
        cur = new Date(y, m + 1, 1);
    }
    return months;
}

function formatDateStr(d) {
    var y = d.getFullYear();
    var m = String(d.getMonth() + 1).padStart(2, '0');
    var dd = String(d.getDate()).padStart(2, '0');
    return y + '-' + m + '-' + dd;
}

function renderAttendancePrintPage() {
    if (attendancePrintCurrentPage < 0 || attendancePrintCurrentPage >= attendancePrintMonths.length) {
        return;
    }

    var monthInfo = attendancePrintMonths[attendancePrintCurrentPage];
    var container = document.getElementById('attendance-print-content');
    if (!container) { return; }

    container.innerHTML = '<div class="loading">加载中...</div>';

    /* 查询该月所有考勤记录 */
    App.apiGet('/api/class/attendance/query?class_id=' + currentClassId +
               '&start_date=' + monthInfo.start + '&end_date=' + monthInfo.end).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '加载失败') + '</div></div>';
            return;
        }

        var records = (res.data.data && res.data.data.records) ? res.data.data.records : [];

        /* 构建日期列表（该月范围内的所有日期） */
        var dateList = buildDateList(monthInfo.start, monthInfo.end);

        /* 构建学生->日期->状态 映射 */
        var studentMap = {};
        var studentOrder = [];
        var refundedNames = {};
        var printStart = attendancePrintMonths[0].start;
        var printEnd = attendancePrintMonths[attendancePrintMonths.length - 1].end;
        /* 用当前班级学生列表作为行顺序（全额退费学生也保留，显示历史考勤） */
        for (var i = 0; i < currentStudents.length; i++) {
            var s = currentStudents[i];
            /* 过滤：学生上课时间与打印时间段无交集则不显示 */
            if (s.student_start_date && s.student_end_date) {
                if (s.student_start_date > printEnd || s.student_end_date < printStart) {
                    continue;
                }
            }
            studentMap[s.student_name] = {};
            studentOrder.push(s.student_name);
            if (s.fully_refunded) { refundedNames[s.student_name] = true; }
            /* 记录部分时段学生的时段信息，用于打印标注 */
            if (s.student_start_date && s.student_end_date) {
                var sStart = s.student_start_date;
                var sEnd = s.student_end_date;
                if (sStart !== currentClassStartTime || sEnd !== currentClassEndTime) {
                    if (!window._studentPeriodMap) { window._studentPeriodMap = {}; }
                    window._studentPeriodMap[s.student_name] = { start: sStart, end: sEnd };
                }
            }
        }
        /* 同时收集记录中可能未在当前学生列表的学生 */
        for (var j = 0; j < records.length; j++) {
            var r = records[j];
            if (!studentMap[r.student_name]) {
                studentMap[r.student_name] = {};
                studentOrder.push(r.student_name);
            }
            studentMap[r.student_name][r.attendance_date] = { status: r.status, leave_time: r.leave_time || '' };
        }

        attendancePrintData = {
            monthInfo: monthInfo,
            dateList: dateList,
            studentOrder: studentOrder,
            studentMap: studentMap,
            refundedNames: refundedNames
        };

        container.innerHTML = buildAttendancePrintHtml(attendancePrintData);
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

function buildDateList(startDate, endDate) {
    var list = [];
    var start = new Date(startDate + 'T00:00:00');
    var end = new Date(endDate + 'T00:00:00');
    var cur = new Date(start);
    while (cur <= end) {
        list.push(formatDateStr(cur));
        cur.setDate(cur.getDate() + 1);
    }
    return list;
}

function buildAttendancePrintHtml(data) {
    var monthInfo = data.monthInfo;
    var dateList = data.dateList;
    var studentOrder = data.studentOrder;
    var studentMap = data.studentMap;
    var refundedNames = data.refundedNames || {};

    var html = '';
    /* 工具栏 */
    html += '<div class="filter-bar" id="print-toolbar">';
    html += '  <span style="align-self:center; font-weight:600;">' + monthInfo.label + ' 考勤表</span>';
    html += '  <span style="align-self:center; font-size:0.85rem; color:#718096;">起始:</span>';
    html += '  <input type="date" id="att-print-start" class="form-input" style="width:auto; padding:4px 8px;" value="' + _esc(attendancePrintMonths[0].start) + '" min="' + _esc(currentClassStartTime) + '" max="' + _esc(currentClassEndTime) + '">';
    html += '  <span style="align-self:center; font-size:0.85rem; color:#718096;">结束:</span>';
    html += '  <input type="date" id="att-print-end" class="form-input" style="width:auto; padding:4px 8px;" value="' + _esc(attendancePrintMonths[attendancePrintMonths.length - 1].end) + '" min="' + _esc(currentClassStartTime) + '" max="' + _esc(currentClassEndTime) + '">';
    html += '  <button class="btn btn-secondary btn-sm" onclick="generateAttendancePrint()">重新生成</button>';
    if (attendancePrintMonths.length > 1) {
        html += '  <span style="align-self:center; color:#718096; font-size:0.85rem;">第 ' + (attendancePrintCurrentPage + 1) + ' / ' + attendancePrintMonths.length + ' 页</span>';
        html += '  <button class="btn btn-secondary btn-sm" onclick="prevPrintPage()"' + (attendancePrintCurrentPage === 0 ? ' disabled' : '') + '>上一页</button>';
        html += '  <button class="btn btn-secondary btn-sm" onclick="nextPrintPage()"' + (attendancePrintCurrentPage >= attendancePrintMonths.length - 1 ? ' disabled' : '') + '>下一页</button>';
    }
    html += '  <button class="btn btn-primary btn-sm" onclick="doPrintAttendance()">打印</button>';
    html += '  <button class="btn btn-secondary btn-sm" onclick="closeAttendancePrint()">关闭</button>';
    html += '</div>';

    /* 打印标题（工具栏打印时隐藏，此标题始终可见） */
    html += '<div id="print-title" style="text-align:center; margin-bottom:12px;">';
    html += '<h2 style="margin:0;">' + _esc(currentClassName) + '考勤</h2>';
    html += '</div>';

    /* 考勤表格 */
    html += '<div id="print-table-wrapper" style="margin-top:12px; overflow:auto;">';
    html += '<table class="attendance-print-table" style="width:100%; border-collapse:collapse; font-size:0.8rem;">';
    /* 提取月份用于表头斜线显示 */
    var monthLabel = monthInfo.label || '';
    var monthShort = monthLabel.replace(/^\d{4}年/, '');
    html += '<thead><tr><th style="border:1px solid #cbd5e0; padding:0; background:#f7fafc; position:sticky; left:0; width:80px; min-width:80px; height:40px;">';
    html += '<div style="position:relative; width:100%; height:100%;">';
    html += '<svg style="position:absolute; top:0; left:0; width:100%; height:100%;" preserveAspectRatio="none"><line x1="0" y1="0" x2="100%" y2="100%" stroke="#cbd5e0" stroke-width="1"/></svg>';
    html += '<span style="position:absolute; left:4px; bottom:2px; font-size:0.75rem; font-weight:600;">姓名</span>';
    html += '<span style="position:absolute; right:4px; top:2px; font-size:0.7rem; color:#4a5568;">' + _esc(monthShort) + '</span>';
    html += '</div></th>';
    for (var i = 0; i < dateList.length; i++) {
        var day = dateList[i].slice(8);
        html += '<th style="border:1px solid #cbd5e0; padding:6px; background:#f7fafc;">' + day + '</th>';
    }
    html += '</tr></thead>';
    html += '<tbody>';
    for (var s = 0; s < studentOrder.length; s++) {
        var name = studentOrder[s];
        var nameHtml = _esc(name);
        if (refundedNames[name]) {
            nameHtml += '<span style="font-size:0.7rem; color:#e53e3e; font-weight:normal; margin-left:4px;">已退费</span>';
        }
        /* 部分时段学生标注时段 */
        var periodMap = window._studentPeriodMap || {};
        if (periodMap[name]) {
            var pp = periodMap[name];
            nameHtml += '<span style="font-size:0.65rem; color:#718096; font-weight:normal; margin-left:2px;">(' + App.formatDate(pp.start).slice(5) + '~' + App.formatDate(pp.end).slice(5) + ')</span>';
        }
        html += '<tr><td style="border:1px solid #cbd5e0; padding:6px; font-weight:600; position:sticky; left:0; background:#fff;' + (refundedNames[name] ? ' color:#a0aec0;' : '') + '">' + nameHtml + '</td>';
        for (var d = 0; d < dateList.length; d++) {
            var cell = studentMap[name][dateList[d]];
            var mark = '';
            var color = '#718096';
            if (cell) {
                if (cell.status === 0) { mark = '✓'; color = '#38a169'; }
                else if (cell.status === 1) { mark = '✗'; color = '#e53e3e'; }
                else if (cell.status === 2) { mark = '-' + cell.leave_time; color = '#dd6b20'; }
                else if (cell.status === 3) { mark = '+' + cell.leave_time; color = '#3182ce'; }
            }
            html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center; color:' + color + ';">' + _esc(mark) + '</td>';
        }
        html += '</tr>';
    }
    if (studentOrder.length === 0) {
        html += '<tr><td colspan="' + (dateList.length + 1) + '" style="border:1px solid #cbd5e0; padding:12px; text-align:center; color:#718096;">暂无学生</td></tr>';
    }
    html += '</tbody></table>';
    html += '</div>';

    /* 图例 */
    html += '<div style="margin-top:8px; font-size:0.8rem; color:#4a5568;">';
    html += '<span style="color:#38a169;">✓</span> 出勤 &nbsp;&nbsp;';
    html += '<span style="color:#e53e3e;">✗</span> 缺勤 &nbsp;&nbsp;';
    html += '<span style="color:#dd6b20;">-HH:MM</span> 早退 &nbsp;&nbsp;';
    html += '<span style="color:#3182ce;">+HH:MM</span> 迟到 &nbsp;&nbsp;';
    html += '空白表示未考勤';
    html += '</div>';

    return html;
}

function prevPrintPage() {
    if (attendancePrintCurrentPage > 0) {
        attendancePrintCurrentPage--;
        renderAttendancePrintPage();
    }
}

function nextPrintPage() {
    if (attendancePrintCurrentPage < attendancePrintMonths.length - 1) {
        attendancePrintCurrentPage++;
        renderAttendancePrintPage();
    }
}

function closeAttendancePrint() {
    var container = document.getElementById('attendance-print-area');
    if (container) { container.style.display = 'none'; container.innerHTML = ''; }
    attendancePrintMonths = [];
    attendancePrintCurrentPage = 0;
    attendancePrintData = {};
}

function doPrintAttendance() {
    var toolbar = document.getElementById('print-toolbar');
    var toolbarDisplay = '';
    if (toolbar) { toolbarDisplay = toolbar.style.display; toolbar.style.display = 'none'; }

    var styleId = 'attendance-print-style';
    var existingStyle = document.getElementById(styleId);
    if (existingStyle) { existingStyle.remove(); }
    var style = document.createElement('style');
    style.id = styleId;
    style.media = 'print';
    style.textContent = 'body * { visibility: hidden; } #attendance-print-content, #attendance-print-content * { visibility: visible; } #attendance-print-content { position: absolute; left: 0; top: 0; width: 100%; } #print-table-wrapper { overflow: visible !important; } .attendance-print-table th, .attendance-print-table td { -webkit-print-color-adjust: exact; print-color-adjust: exact; }';
    document.head.appendChild(style);

    window.print();

    /* 恢复 */
    setTimeout(function() {
        if (toolbar) { toolbar.style.display = toolbarDisplay; }
        if (style) { style.remove(); }
    }, 500);
}

/* --- Student query --- */
var studentQueryData = [];

function showStudentQuery() {
    document.getElementById('section-class-list').style.display = 'none';
    document.getElementById('section-student-query').style.display = 'block';
    /* Default date range: this month */
    var now = new Date();
    var y = now.getFullYear();
    var m = String(now.getMonth() + 1).padStart(2, '0');
    var firstDay = y + '-' + m + '-01';
    var lastDay = y + '-' + m + '-' + String(new Date(y, now.getMonth() + 1, 0).getDate()).padStart(2, '0');
    var startEl = document.getElementById('query-start-date');
    var endEl = document.getElementById('query-end-date');
    if (startEl && !startEl.value) { startEl.value = firstDay; }
    if (endEl && !endEl.value) { endEl.value = lastDay; }
    window.scrollTo(0, 0);
}

function onQueryTypeChange() {
    var typeEl = document.getElementById('query-type');
    studentQueryType = typeEl ? typeEl.value : 'registration';
    /* Clear previous results */
    var container = document.getElementById('student-query-result');
    if (container) { container.innerHTML = ''; }
    studentQueryData = [];
}

function loadStudentQuery() {
    var typeEl = document.getElementById('query-type');
    studentQueryType = typeEl ? typeEl.value : 'registration';

    if (studentQueryType === 'allocation') {
        loadAllocationQuery();
    } else if (studentQueryType === 'student_type') {
        loadStudentTypeQuery();
    } else {
        loadRegistrationQuery();
    }
}

function loadRegistrationQuery() {
    var container = document.getElementById('student-query-result');
    if (!container) { return; }

    var startDate = document.getElementById('query-start-date').value;
    var endDate = document.getElementById('query-end-date').value;
    if (!startDate || !endDate) {
        App.showToast('请选择起止日期', 'error');
        return;
    }

    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/class/students/query?start_time=' + startDate + '&end_time=' + endDate).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '查询失败') + '</div></div>';
            return;
        }

        studentQueryData = res.data.data.students || [];
        if (studentQueryData.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128100;</div><div class="empty-text">该时间段内暂无报名学生</div></div>';
            return;
        }

        container.innerHTML = buildRegistrationQueryHtml(studentQueryData);
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

function loadStudentTypeQuery() {
    var container = document.getElementById('student-query-result');
    if (!container) { return; }

    var startDate = document.getElementById('query-start-date').value;
    var endDate = document.getElementById('query-end-date').value;
    if (!startDate || !endDate) {
        App.showToast('请选择起止日期', 'error');
        return;
    }

    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/class/students/query?start_time=' + startDate + '&end_time=' + endDate).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '查询失败') + '</div></div>';
            return;
        }

        studentQueryData = res.data.data.students || [];
        if (studentQueryData.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128100;</div><div class="empty-text">该时间段内暂无报名学生</div></div>';
            return;
        }

        container.innerHTML = buildStudentTypeQueryHtml(studentQueryData);
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

function loadAllocationQuery() {
    var container = document.getElementById('student-query-result');
    if (!container) { return; }

    var startDate = document.getElementById('query-start-date').value;
    var endDate = document.getElementById('query-end-date').value;
    if (!startDate || !endDate) {
        App.showToast('请选择起止日期', 'error');
        return;
    }

    container.innerHTML = '<div class="loading">加载中...</div>';

    App.apiGet('/api/class/allocations/query?start_time=' + startDate + '&end_time=' + endDate).then(function(res) {
        if (res.data.code !== 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-text">' + (res.data.message || '查询失败') + '</div></div>';
            return;
        }

        var data = res.data.data || {};
        studentQueryData = data.allocations || [];
        studentQueryData._resources = data.resources || [];
        if (studentQueryData.length === 0) {
            container.innerHTML = '<div class="empty-state"><div class="empty-icon">&#128100;</div><div class="empty-text">该时间段内暂无资源分配记录</div></div>';
            return;
        }

        container.innerHTML = buildAllocationQueryHtml(studentQueryData, studentQueryData._resources);
    }).catch(function() {
        container.innerHTML = '<div class="empty-state"><div class="empty-text">网络错误</div></div>';
    });
}

function buildRegistrationQueryHtml(students) {
    var totalPrice = 0;
    var totalPaid = 0;
    var html = '<div id="student-query-table-wrapper" style="margin-top:8px; overflow:auto;">';
    html += '<table class="student-query-table" style="width:100%; border-collapse:collapse; font-size:0.85rem;">';
    html += '<thead><tr>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">序号</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">姓名</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">性别</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">家长电话</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">班级</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">报名方式</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">实缴(元)</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">招收老师</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">报名时间</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">结束服务时间</th>';
    html += '</tr></thead>';
    html += '<tbody>';
    for (var i = 0; i < students.length; i++) {
        var s = students[i];
        var gender = s.student_gender === 'male' ? '男' : '女';
        var priceNum = parseFloat(s.price) || 0;
        var paidNum = (s.paid_amount !== undefined && s.paid_amount !== null)
            ? (parseFloat(s.paid_amount) || 0)
            : priceNum;
        totalPrice += priceNum;
        totalPaid += paidNum;
        var paidStr = paidNum.toFixed(2);
        var refundTag = '';
        var refundNum = parseFloat(s.refund_amount) || 0;
        if (refundNum > 0.001) {
            refundTag = ' <span class="tag tag-orange" style="margin-left:4px;">已退' + refundNum.toFixed(2) + '</span>';
        }
        var depositTag = '';
        if (s.is_deposit === 1) {
            var depositAmt = parseFloat(s.paid_amount_snapshot) || 0;
            depositTag = ' <span class="tag tag-blue" style="margin-left:4px;">定金' + depositAmt.toFixed(1) + '元</span>';
        }
        /* 部分时段标注 */
        var periodTag = '';
        if (s.student_start_date && s.student_end_date) {
            var qClassStart = s.class_start_time || '';
            var qClassEnd = s.class_end_time || '';
            if (qClassStart && qClassEnd && (s.student_start_date !== qClassStart || s.student_end_date !== qClassEnd)) {
                periodTag = ' <span class="tag tag-gray" style="margin-left:4px;">' + App.formatDate(s.student_start_date).slice(5) + '~' + App.formatDate(s.student_end_date).slice(5) + '</span>';
            }
        }
        html += '<tr>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + (i + 1) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; font-weight:700;">' + _esc(s.student_name) + periodTag + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + gender + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.parent_phone) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.class_name) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.activity_name) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:right;">' + paidStr + refundTag + depositTag + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.teacher_name) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.register_time) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + (s.student_end_date ? App.formatDate(s.student_end_date) : '') + '</td>';
        html += '</tr>';
    }
    /* 总计行 */
    html += '<tr style="font-weight:700; background:#f7fafc;">';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;" colspan="2">合计: ' + students.length + ' 人</td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:right;">' + totalPaid.toFixed(2) + '</td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '</tr>';
    html += '</tbody></table>';
    html += '</div>';
    return html;
}

function buildStudentTypeQueryHtml(students) {
    var html = '<div id="student-query-table-wrapper" style="margin-top:8px; overflow:auto;">';
    html += '<table class="student-query-table" style="width:100%; border-collapse:collapse; font-size:0.85rem;">';
    html += '<thead><tr>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">序号</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">姓名</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">性别</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">家长电话</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">所在班级</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">学生备注</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">过敏信息</th>';
    html += '</tr></thead>';
    html += '<tbody>';
    for (var i = 0; i < students.length; i++) {
        var s = students[i];
        var gender = s.student_gender === 'male' ? '男' : '女';
        var allergyText = '';
        if (s.has_allergy === 1) {
            allergyText = _esc(s.allergy_desc) || '是';
        } else {
            allergyText = '无';
        }
        html += '<tr>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + (i + 1) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; font-weight:700;">' + _esc(s.student_name) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + gender + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.parent_phone) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.class_name) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(s.other_info) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + allergyText + '</td>';
        html += '</tr>';
    }
    /* 总计行 */
    html += '<tr style="font-weight:700; background:#f7fafc;">';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;" colspan="7">合计: ' + students.length + ' 人</td>';
    html += '</tr>';
    html += '</tbody></table>';
    html += '</div>';
    return html;
}

function buildAllocationQueryHtml(allocs, resources) {
    /* Group allocations by student (registration_id) */
    var students = {};
    var studentOrder = [];
    for (var i = 0; i < allocs.length; i++) {
        var a = allocs[i];
        var key = a.registration_id > 0 ? a.registration_id : (a.student_name + '|' + a.class_name);
        if (!students[key]) {
            students[key] = { student_name: a.student_name, student_gender: a.student_gender, class_name: a.class_name, resources: {} };
            studentOrder.push(key);
        }
        if (a.resource_id > 0) {
            students[key].resources[a.resource_id] = a.resource_code;
        }
    }

    var html = '<div id="student-query-table-wrapper" style="margin-top:8px; overflow:auto;">';
    html += '<table class="student-query-table" style="width:100%; border-collapse:collapse; font-size:0.85rem;">';
    html += '<thead><tr>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">序号</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">姓名</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">性别</th>';
    html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">所在班级</th>';
    for (var r = 0; r < resources.length; r++) {
        html += '<th style="border:1px solid #cbd5e0; padding:8px; background:#f7fafc;">' + _esc(resources[r].name) + '</th>';
    }
    html += '</tr></thead>';
    html += '<tbody>';
    for (var i = 0; i < studentOrder.length; i++) {
        var st = students[studentOrder[i]];
        var gender = st.student_gender === 'male' ? '男' : '女';
        html += '<tr>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + (i + 1) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; font-weight:700;">' + _esc(st.student_name) + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + gender + '</td>';
        html += '<td style="border:1px solid #cbd5e0; padding:6px;">' + _esc(st.class_name) + '</td>';
        for (var r = 0; r < resources.length; r++) {
            var code = st.resources[resources[r].id];
            html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + (code !== undefined ? code : '-') + '</td>';
        }
        html += '</tr>';
    }
    /* 总计行 */
    html += '<tr style="font-weight:700; background:#f7fafc;">';
    var totalCols = 4 + resources.length;
    html += '<td style="border:1px solid #cbd5e0; padding:6px;" colspan="2">合计: ' + studentOrder.length + ' 人</td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    html += '<td style="border:1px solid #cbd5e0; padding:6px;"></td>';
    for (var r = 0; r < resources.length; r++) {
        var count = 0;
        for (var k = 0; k < studentOrder.length; k++) {
            if (students[studentOrder[k]].resources[resources[r].id] !== undefined) { count++; }
        }
        html += '<td style="border:1px solid #cbd5e0; padding:6px; text-align:center;">' + count + '</td>';
    }
    html += '</tr>';
    html += '</tbody></table>';
    html += '</div>';
    return html;
}

function printStudentQuery() {
    if (studentQueryData.length === 0) {
        App.showToast('请先查询数据', 'error');
        return;
    }

    var startDate = document.getElementById('query-start-date').value;
    var endDate = document.getElementById('query-end-date').value;
    var printArea = document.getElementById('student-query-print-area');
    if (!printArea) { return; }

    /* 构建打印内容 */
    var html = '<div style="text-align:center; margin-bottom:16px;">';
    if (studentQueryType === 'allocation') {
        html += '<h2 style="margin:0;">' + startDate + '至' + endDate + '资源统计表</h2>';
    } else if (studentQueryType === 'student_type') {
        html += '<h2 style="margin:0;">' + startDate + '至' + endDate + '学生统计表</h2>';
    } else {
        html += '<h2 style="margin:0;">' + startDate + '至' + endDate + '报名统计表</h2>';
    }
    html += '</div>';

    if (studentQueryType === 'allocation') {
        html += buildAllocationQueryHtml(studentQueryData, studentQueryData._resources || []);
    } else if (studentQueryType === 'student_type') {
        html += buildStudentTypeQueryHtml(studentQueryData);
    } else {
        html += buildRegistrationQueryHtml(studentQueryData);
    }

    printArea.innerHTML = html;
    printArea.style.display = 'block';

    /* 打印样式：只打印打印区域 */
    var styleId = 'student-query-print-style';
    var existingStyle = document.getElementById(styleId);
    if (existingStyle) { existingStyle.remove(); }
    var style = document.createElement('style');
    style.id = styleId;
    style.media = 'print';
    style.textContent = 'body * { visibility: hidden; } #student-query-print-area, #student-query-print-area * { visibility: visible; } #student-query-print-area { position: absolute; left: 0; top: 0; width: 100%; } #student-query-table-wrapper { overflow: visible !important; } .student-query-table th, .student-query-table td { -webkit-print-color-adjust: exact; print-color-adjust: exact; }';
    document.head.appendChild(style);

    window.print();

    /* 恢复 */
    setTimeout(function() {
        printArea.style.display = 'none';
        if (style) { style.remove(); }
    }, 500);
}

/* --- Utility --- */
function _esc(s) {
    if (!s) { return ''; }
    var div = document.createElement('div');
    div.appendChild(document.createTextNode(s));
    return div.innerHTML;
}

/* --- Refund / Cancel Refund --- */

/* 打开退费弹窗：预填学生姓名和当前实缴 */
function openRefundModal(regId, studentName, currentPaid) {
    var modal = document.getElementById('refund-modal');
    if (!modal) {
        App.showToast('退费弹窗未初始化', 'error');
        return;
    }
    document.getElementById('refund-reg-id').value = regId;
    document.getElementById('refund-stu-name').textContent = studentName;
    document.getElementById('refund-current-paid').textContent = parseFloat(currentPaid || 0).toFixed(2) + ' 元';
    document.getElementById('refund-amount').value = '';
    modal.style.display = 'flex';
}

/* 关闭退费弹窗 */
function closeRefundModal() {
    var modal = document.getElementById('refund-modal');
    if (modal) { modal.style.display = 'none'; }
}

/* 确认退费 */
function confirmRefund() {
    var regId = parseInt(document.getElementById('refund-reg-id').value, 10);
    var amountStr = document.getElementById('refund-amount').value;
    var amount = parseFloat(amountStr);
    if (isNaN(amount) || amount < 0) {
        App.showToast('请输入有效的退费金额', 'error');
        return;
    }
    App.apiPost('/api/class/students/refund', {
        registration_id: regId,
        refund_amount: amount
    }).then(function(res) {
        if (res.data.code === 0) {
            var paid = parseFloat(res.data.data.paid_amount || 0).toFixed(2);
            App.showToast('退费成功，当前实缴 ' + paid + ' 元', 'success');
            closeRefundModal();
            loadStudents();
        } else if (res.data.code === 5006) {
            App.showToast('不可退费超过报名金额的钱', 'error');
        } else if (res.data.code === 5007) {
            App.showToast(res.data.message || '超过考勤折损上限', 'error');
        } else if (res.data.code === 5003) {
            App.showToast('报名记录不存在', 'error');
            closeRefundModal();
        } else {
            App.showToast(res.data.message || '退费失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* 撤销退费（直接调接口，弹确认框） */
function cancelRefund(regId, studentName) {
    if (!confirm('确认撤销 ' + studentName + ' 的最近一次退费吗？撤销后实缴金额将恢复。')) {
        return;
    }
    App.apiPost('/api/class/students/refund/cancel', {
        registration_id: regId
    }).then(function(res) {
        if (res.data.code === 0) {
            var paid = parseFloat(res.data.data.paid_amount || 0).toFixed(2);
            App.showToast('已撤销退费，实缴恢复为 ' + paid + ' 元', 'success');
            loadStudents();
        } else if (res.data.code === 5005) {
            App.showToast('没有可撤销的退费记录', 'error');
        } else {
            App.showToast(res.data.message || '撤销退费失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

function deleteStudent(regId, studentName) {
    if (!confirm('确定删除学生' + studentName + '？此操作不可恢复')) {
        return;
    }
    App.apiPost('/api/class/student/delete', {
        registration_id: regId
    }).then(function(res) {
        if (res.data.code === 0) {
            App.showToast('删除成功', 'success');
            loadStudents();
        } else {
            App.showToast(res.data.message || '删除失败', 'error');
        }
    }).catch(function() {
        App.showToast('网络错误', 'error');
    });
}

/* --- Renew (extend student period) --- */
var _renewAmountManuallyModified = false;

function openRenewModal() {
    var ctx = window._renewContext;
    if (!ctx || !ctx.regId) {
        App.showToast('续费信息缺失，请重新打开编辑', 'error');
        return;
    }
    if (!currentClassId) {
        App.showToast('请先选择班级', 'error');
        return;
    }

    _renewAmountManuallyModified = false;

    /* 填充当前时段 */
    var currentPeriodEl = document.getElementById('renew-current-period');
    if (currentPeriodEl) {
        currentPeriodEl.value = App.formatDate(ctx.studentStart) + ' ~ ' + App.formatDate(ctx.studentEnd);
    }

    /* 新时段结束日期：默认=班级结束日期，max=班级结束日期 */
    var newEndEl = document.getElementById('renew-new-end');
    if (newEndEl) {
        newEndEl.value = ctx.classEnd;
        newEndEl.min = addOneDay(ctx.studentEnd);
        newEndEl.max = ctx.classEnd;
    }

    /* 续费金额清空 */
    var amountEl = document.getElementById('renew-amount');
    if (amountEl) { amountEl.value = ''; }
    var hintEl = document.getElementById('renew-suggested-hint');
    if (hintEl) { hintEl.textContent = ''; }

    /* 隐藏错误 */
    var errEl = document.getElementById('renew-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    /* 存 reg id */
    document.getElementById('renew-reg-id').value = ctx.regId;

    /* 显示续费时段 */
    updateRenewPeriodDisplay();

    /* 计算建议续费金额 */
    calculateSuggestedRenewAmount();

    App.showModal('renew-modal');
}

function addOneDay(dateStr) {
    /* dateStr format: YYYY-MM-DD, returns YYYY-MM-DD + 1 day */
    if (!dateStr) { return ''; }
    var d = new Date(dateStr + 'T00:00:00');
    if (isNaN(d.getTime())) { return dateStr; }
    d.setDate(d.getDate() + 1);
    var y = d.getFullYear();
    var m = String(d.getMonth() + 1).padStart(2, '0');
    var dd = String(d.getDate()).padStart(2, '0');
    return y + '-' + m + '-' + dd;
}

function updateRenewPeriodDisplay() {
    var ctx = window._renewContext;
    if (!ctx) { return; }
    var newEndEl = document.getElementById('renew-new-end');
    var newEnd = newEndEl ? newEndEl.value : '';
    var displayEl = document.getElementById('renew-period-display');
    if (displayEl) {
        var renewStart = addOneDay(ctx.studentEnd);
        displayEl.value = newEnd ? (App.formatDate(renewStart) + ' ~ ' + App.formatDate(newEnd)) : '';
    }
    calculateSuggestedRenewAmount();
}

function calculateSuggestedRenewAmount() {
    var ctx = window._renewContext;
    if (!ctx || !currentClassId) { return; }

    var newEndEl = document.getElementById('renew-new-end');
    var newEnd = newEndEl ? newEndEl.value : '';
    if (!newEnd || !ctx.studentStart || !ctx.priceId) { return; }

    /* 续费时段 = studentEnd+1 ~ newEnd（仅计算延长部分） */
    var renewStart = addOneDay(ctx.studentEnd);
    App.apiGet('/api/class/calculate-amount?class_id=' + currentClassId
               + '&start=' + encodeURIComponent(renewStart)
               + '&end=' + encodeURIComponent(newEnd)
               + '&price_id=' + ctx.priceId).then(function(res) {
        if (res.data.code === 0) {
            var suggested = parseFloat(res.data.data.suggested_amount) || 0;
            var hintEl = document.getElementById('renew-suggested-hint');
            if (hintEl) {
                hintEl.textContent = '系统建议：' + suggested.toFixed(2) + ' 元';
            }
            if (!_renewAmountManuallyModified) {
                var amountEl = document.getElementById('renew-amount');
                if (amountEl) { amountEl.value = suggested.toFixed(2); }
            }
        }
    }).catch(function() {
        /* ignore calculation failure */
    });
}

function confirmRenew() {
    var regId = parseInt(document.getElementById('renew-reg-id').value, 10);
    var newEnd = document.getElementById('renew-new-end').value;
    var amountStr = document.getElementById('renew-amount').value;
    var amount = parseFloat(amountStr);

    /* 校验 */
    var errEl = document.getElementById('renew-error');
    if (errEl) { errEl.textContent = ''; errEl.classList.remove('show'); }

    if (!newEnd) {
        _showRenewError('请选择新时段结束日期');
        return;
    }
    if (isNaN(amount) || amount < 0) {
        _showRenewError('请输入有效的续费金额（>=0）');
        return;
    }

    /* 显示二次确认弹窗 */
    var confirmTextEl = document.getElementById('renew-confirm-amount-text');
    if (confirmTextEl) { confirmTextEl.textContent = amount.toFixed(2); }
    App.showModal('renew-confirm-modal');
}

function submitRenew() {
    var regId = parseInt(document.getElementById('renew-reg-id').value, 10);
    var newEnd = document.getElementById('renew-new-end').value;
    var amount = parseFloat(document.getElementById('renew-amount').value);

    App.apiPost('/api/class/students/renew', {
        registration_id: regId,
        new_end_date: newEnd,
        renew_amount: amount
    }).then(function(res) {
        if (res.data.code === 0) {
            App.hideModal('renew-confirm-modal');
            App.hideModal('renew-modal');
            App.hideModal('student-edit-modal');
            App.showToast('续费成功', 'success');
            loadStudents();
        } else if (res.data.code === 5016) {
            App.hideModal('renew-confirm-modal');
            _showRenewError(res.data.message || '续费日期无效');
        } else if (res.data.code === 5017) {
            App.hideModal('renew-confirm-modal');
            _showRenewError(res.data.message || '该学生不允许续费');
        } else if (res.data.code === 5018) {
            App.hideModal('renew-confirm-modal');
            _showRenewError(res.data.message || '续费金额非法');
        } else if (res.data.code === 4004) {
            App.hideModal('renew-confirm-modal');
            _showRenewError('名额不足，无法续费');
        } else {
            App.hideModal('renew-confirm-modal');
            _showRenewError(res.data.message || '续费失败');
        }
    }).catch(function() {
        App.hideModal('renew-confirm-modal');
        _showRenewError('网络错误，请重试');
    });
}

function _showRenewError(msg) {
    var errEl = document.getElementById('renew-error');
    if (errEl) {
        errEl.textContent = msg;
        errEl.classList.add('show');
    }
}
