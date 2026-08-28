/* app.js - Common utilities for registerStudent */

var App = (function() {
    /* --- Cookie helpers --- */
    function getCookie(name) {
        var cookies = document.cookie.split(';');
        for (var i = 0; i < cookies.length; i++) {
            var c = cookies[i].trim();
            if (c.indexOf(name + '=') === 0) {
                return c.substring(name.length + 1);
            }
        }
        return '';
    }

    function setCookie(name, value, days) {
        var expires = '';
        if (days) {
            var d = new Date();
            d.setTime(d.getTime() + days * 86400000);
            expires = '; expires=' + d.toUTCString();
        }
        document.cookie = name + '=' + value + expires + '; path=/';
    }

    function deleteCookie(name) {
        document.cookie = name + '=; path=/; max-age=0';
    }

    /* --- API helpers --- */
    function apiRequest(method, url, body) {
        var opts = {
            method: method,
            headers: { 'Content-Type': 'application/json' },
            credentials: 'same-origin',
            cache: 'no-store'
        };
        if (body !== undefined && body !== null) {
            opts.body = JSON.stringify(body);
        }
        return fetch(url, opts).then(function(res) {
            return res.json().then(function(data) {
                return { status: res.status, data: data };
            });
        });
    }

    function apiGet(url) {
        return apiRequest('GET', url, null);
    }

    function apiPost(url, body) {
        return apiRequest('POST', url, body);
    }

    function apiPut(url, body) {
        return apiRequest('PUT', url, body);
    }

    function apiDelete(url, body) {
        return apiRequest('DELETE', url, body);
    }

    /* --- Session check --- */
    function checkSession(callback, requiredRole) {
        apiGet('/api/auth/session').then(function(res) {
            if (res.data.code !== 0) {
                var loginUrl = '/login';
                if (typeof requiredRole !== 'undefined') {
                    loginUrl += '?role=' + requiredRole;
                }
                window.location.replace(loginUrl);
                return;
            }
            if (typeof requiredRole !== 'undefined' && res.data.data.role !== requiredRole) {
                App.showToast('需要管理员权限', 'error');
                setTimeout(function() { window.location.replace('/'); }, 1000);
                return;
            }
            if (callback) {
                callback(res.data.data);
            }
        }).catch(function() {
            var loginUrl = '/login';
            if (typeof requiredRole !== 'undefined') {
                loginUrl += '?role=' + requiredRole;
            }
            window.location.replace(loginUrl);
        });
    }

    /* Guard a page that requires login. On pageshow from bfcache, re-check session. */
    function requireLogin(callback, requiredRole) {
        checkSession(callback, requiredRole);
        window.addEventListener('pageshow', function(e) {
            if (e.persisted) {
                checkSession(callback, requiredRole);
            }
        });
    }

    /* Optional session check: does NOT redirect on unauthenticated.
       Used by pages that allow anonymous access (e.g. registration). */
    function checkSessionOptional(callback) {
        apiGet('/api/auth/session').then(function(res) {
            if (res.data.code === 0 && callback) {
                callback(res.data.data);
            } else if (callback) {
                callback(null);
            }
        }).catch(function() {
            if (callback) { callback(null); }
        });
    }

    /* --- Check admin exists --- */
    function checkAdminExists(callback) {
        apiGet('/api/auth/check-admin').then(function(res) {
            if (res.data.code === 0) {
                callback(res.data.data.exists);
            }
        });
    }

    /* --- Toast --- */
    function showToast(msg, type, duration) {
        type = type || 'info';
        duration = duration || 2500;
        var el = document.createElement('div');
        el.className = 'toast toast-' + type;
        el.textContent = msg;
        document.body.appendChild(el);
        setTimeout(function() { el.classList.add('show'); }, 10);
        setTimeout(function() {
            el.classList.remove('show');
            setTimeout(function() { el.remove(); }, 300);
        }, duration);
    }

    /* --- Context menu (right-click + long-press) --- */
    var _ctxMenu = null;
    var _longPressTimer = null;

    function showContextMenu(e, items) {
        e.preventDefault();
        e.stopPropagation();
        _removeContextMenu();

        var menu = document.createElement('div');
        menu.className = 'context-menu';

        for (var i = 0; i < items.length; i++) {
            var btn = document.createElement('button');
            btn.className = 'context-menu-item' + (items[i].danger ? ' danger' : '');
            btn.textContent = items[i].label;
            btn.setAttribute('data-idx', i);
            btn.addEventListener('click', (function(idx) {
                return function() {
                    items[idx].action();
                    _removeContextMenu();
                };
            })(i));
            menu.appendChild(btn);
        }

        document.body.appendChild(menu);
        _ctxMenu = menu;

        /* position */
        var x = e.clientX || (e.touches && e.touches[0] ? e.touches[0].clientX : 0);
        var y = e.clientY || (e.touches && e.touches[0] ? e.touches[0].clientY : 0);
        var mw = menu.offsetWidth;
        var mh = menu.offsetHeight;
        if (x + mw > window.innerWidth) { x = window.innerWidth - mw - 8; }
        if (y + mh > window.innerHeight) { y = window.innerHeight - mh - 8; }
        menu.style.left = x + 'px';
        menu.style.top = y + 'px';
        menu.classList.add('show');
    }

    function _removeContextMenu() {
        if (_ctxMenu) {
            _ctxMenu.remove();
            _ctxMenu = null;
        }
    }

    document.addEventListener('click', function() {
        _removeContextMenu();
    });

    /* Long-press support for mobile */
    function bindLongPress(el, callback) {
        var timer = null;
        el.addEventListener('touchstart', function(e) {
            timer = setTimeout(function() {
                callback(e);
            }, 500);
        }, { passive: true });
        el.addEventListener('touchend', function() {
            clearTimeout(timer);
        });
        el.addEventListener('touchmove', function() {
            clearTimeout(timer);
        });
        el.addEventListener('contextmenu', function(e) {
            e.preventDefault();
            callback(e);
        });
    }

    /* --- Modal helpers --- */
    function showModal(id) {
        var el = document.getElementById(id);
        if (el) { el.classList.add('show'); }
    }

    function hideModal(id) {
        var el = document.getElementById(id);
        if (el) { el.classList.remove('show'); }
    }

    /* --- QR code modal --- */
    function showQrcode(src) {
        var overlay = document.getElementById('qrcode-modal');
        var img = document.getElementById('qrcode-img');
        if (overlay && img) {
            img.src = src;
            overlay.classList.add('show');
        }
    }

    /* --- Pagination helper --- */
    function renderPagination(containerId, total, page, pageSize, onPageChange) {
        var container = document.getElementById(containerId);
        if (!container) { return; }
        container.innerHTML = '';

        var totalPages = Math.ceil(total / pageSize);
        if (totalPages <= 1) { return; }

        var wrap = document.createElement('div');
        wrap.className = 'pagination';

        var prevBtn = document.createElement('button');
        prevBtn.className = 'btn btn-secondary btn-sm page-btn';
        prevBtn.textContent = '\u4E0A\u4E00\u9875';
        prevBtn.disabled = (page <= 1);
        prevBtn.addEventListener('click', function() { onPageChange(page - 1); });
        wrap.appendChild(prevBtn);

        var info = document.createElement('span');
        info.className = 'page-info';
        info.textContent = page + ' / ' + totalPages + ' (\u5171' + total + '\u6761)';
        wrap.appendChild(info);

        var nextBtn = document.createElement('button');
        nextBtn.className = 'btn btn-secondary btn-sm page-btn';
        nextBtn.textContent = '\u4E0B\u4E00\u9875';
        nextBtn.disabled = (page >= totalPages);
        nextBtn.addEventListener('click', function() { onPageChange(page + 1); });
        wrap.appendChild(nextBtn);

        container.appendChild(wrap);
    }

    /* --- Format date --- */
    function formatDate(dateStr) {
        if (!dateStr) { return ''; }
        return dateStr.substring(0, 10);
    }

    /* --- Gender label --- */
    function genderLabel(g) {
        if (g === 'male') { return '\u7537'; }
        if (g === 'female') { return '\u5973'; }
        return g;
    }

    /* --- Op type label --- */
    var opTypeMap = {
        1: '\u5B66\u751F\u62A5\u540D',
        2: '\u5206\u914D\u8D44\u6E90',
        3: '\u91CA\u653E\u8D44\u6E90',
        4: '\u91CD\u7F6E\u5BC6\u7801',
        5: '\u4FEE\u6539\u540D\u989D',
        6: '\u521B\u5EFA\u73ED\u7EA7',
        7: '\u521B\u5EFA\u8D44\u6E90',
        8: '\u4FEE\u6539\u8D44\u6E90',
        9: '\u5220\u9664\u8D44\u6E90',
        10: '\u5BA1\u6279\u91CD\u7F6E',
        11: '\u5220\u9664\u7528\u6237',
        12: '\u4FEE\u6539\u5B66\u751F'
    };

    function opTypeLabel(t) {
        return opTypeMap[t] || ('\u64CD\u4F5C' + t);
    }

    /* --- Navigate --- */
    function navigateTo(path) {
        window.location.href = path;
    }

    /* --- Password visibility toggle --- */
    function togglePassword(inputId) {
        var input = document.getElementById(inputId);
        if (!input) { return; }
        var btn = input.parentElement.querySelector('.pwd-toggle');
        if (input.type === 'password') {
            input.type = 'text';
            if (btn) { btn.innerHTML = '&#128064;'; }
        } else {
            input.type = 'password';
            if (btn) { btn.innerHTML = '&#128065;'; }
        }
    }

    function compressImage(file, maxDim, quality, callback) {
        var reader = new FileReader();
        reader.onload = function(e) {
            var img = new Image();
            img.onload = function() {
                var w = img.width, h = img.height;
                if (w <= maxDim && h <= maxDim && file.type === 'image/jpeg') {
                    callback(e.target.result.split(',')[1], file.name);
                    return;
                }
                if (w > maxDim || h > maxDim) {
                    if (w > h) { h = Math.round(h * maxDim / w); w = maxDim; }
                    else { w = Math.round(w * maxDim / h); h = maxDim; }
                }
                var canvas = document.createElement('canvas');
                canvas.width = w;
                canvas.height = h;
                var ctx = canvas.getContext('2d');
                ctx.imageSmoothingEnabled = true;
                ctx.imageSmoothingQuality = 'high';
                ctx.drawImage(img, 0, 0, w, h);
                var dataUrl = canvas.toDataURL('image/jpeg', quality);
                var ext = '.jpg';
                var name = file.name.replace(/\.[^.]+$/, '') + ext;
                callback(dataUrl.split(',')[1], name);
            };
            img.onerror = function() {
                callback(e.target.result.split(',')[1], file.name);
            };
            img.src = e.target.result;
        };
        reader.readAsDataURL(file);
    }

    function showConfirm(msg, onConfirm) {
        var overlay = document.createElement('div');
        overlay.className = 'modal-overlay show';
        overlay.style.zIndex = '10000';

        var content = document.createElement('div');
        content.className = 'modal-content';
        content.style.cssText = 'max-width:380px; text-align:center;';

        var body = document.createElement('div');
        body.style.cssText = 'padding:20px 16px 12px; font-size:15px; color:#333;';
        body.textContent = msg;
        content.appendChild(body);

        var btnRow = document.createElement('div');
        btnRow.style.cssText = 'display:flex; gap:12px; justify-content:center; padding:0 16px 20px;';

        var cancelBtn = document.createElement('button');
        cancelBtn.className = 'btn btn-secondary';
        cancelBtn.textContent = '\u53D6\u6D88';
        cancelBtn.style.cssText = 'min-width:80px; order:1;';

        var okBtn = document.createElement('button');
        okBtn.className = 'btn btn-primary';
        okBtn.textContent = '\u786E\u8BA4';
        okBtn.style.cssText = 'min-width:80px; order:2;';

        btnRow.appendChild(cancelBtn);
        btnRow.appendChild(okBtn);
        content.appendChild(btnRow);
        overlay.appendChild(content);
        document.body.appendChild(overlay);

        function close() { overlay.remove(); }

        cancelBtn.addEventListener('click', close);
        okBtn.addEventListener('click', function() {
            close();
            if (onConfirm) { onConfirm(); }
        });
        overlay.addEventListener('click', function(e) {
            if (e.target === overlay) { close(); }
        });
        okBtn.focus();
    }

    /* --- Public API --- */
    return {
        getCookie: getCookie,
        setCookie: setCookie,
        deleteCookie: deleteCookie,
        apiGet: apiGet,
        apiPost: apiPost,
        apiPut: apiPut,
        apiDelete: apiDelete,
        checkSession: checkSession,
        checkSessionOptional: checkSessionOptional,
        requireLogin: requireLogin,
        checkAdminExists: checkAdminExists,
        showToast: showToast,
        showContextMenu: showContextMenu,
        bindLongPress: bindLongPress,
        showModal: showModal,
        hideModal: hideModal,
        showQrcode: showQrcode,
        renderPagination: renderPagination,
        formatDate: formatDate,
        genderLabel: genderLabel,
        opTypeLabel: opTypeLabel,
        navigateTo: navigateTo,
        togglePassword: togglePassword,
        compressImage: compressImage,
        showConfirm: showConfirm
    };
})();
