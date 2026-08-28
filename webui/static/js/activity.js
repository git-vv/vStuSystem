/* activity.js - Public activity signup page */

(function() {
    'use strict';

    var _activities = [];
    var _currentActivityId = 0;
    var _promotionImages = [];

    /* Group signup state */
    var _groupId = 0;
    var _isLeader = false;
    var _pollTimer = null;
    var _myName = '';
    var _myPhone = '';
    var _isGroupMode = false;
    var _groupType = 0; /* 0=normal, 1=session group, 2=sync group */
    var _createdAtEpoch = 0;
    var _groupTimeout = 1800; /* 30 minutes */

    /* Session group sharing state */
    var _inviteCode = '';
    var _activityTitle = '';
    var _targetCount = 0;

    /* Pagination state */
    var _currentPage = 1;
    var _pageSize = 6;
    var _hasMore = false;
    var _isLoadingMore = false;
    var _totalCount = 0;

    /* HTML escape helper */
    function _esc(s) {
        if (!s) { return ''; }
        var div = document.createElement('div');
        div.textContent = s;
        return div.innerHTML;
    }

    function loadPromotion() {
        App.apiGet('/api/public/activity/promotion').then(function(res) {
            if (res.data.code !== 0) { return; }
            var data = res.data.data;
            var images = data.images || [];
            var text = data.text || '';

            if (images.length === 0 && !text) { return; }

            _promotionImages = images;
            var section = document.getElementById('promotion-section');
            section.style.display = '';

            if (images.length > 0) {
                var carouselEl = document.getElementById('promotion-carousel');
                var html = '<div class="carousel" data-index="0" data-count="' + images.length + '">';
                html += '<div class="carousel-track">';
                for (var i = 0; i < images.length; i++) {
                    html += '<img class="carousel-slide" data-promo-idx="' + i + '" src="' + _esc(images[i].path) + '" alt="promotion" loading="lazy">';
                }
                html += '</div>';
                if (images.length > 1) {
                    html += '<div class="carousel-dots">';
                    for (var i = 0; i < images.length; i++) {
                        html += '<span class="carousel-dot' + (i === 0 ? ' active' : '') + '" data-index="' + i + '"></span>';
                    }
                    html += '</div>';
                }
                html += '</div>';
                carouselEl.innerHTML = html;

                var slides = carouselEl.querySelectorAll('.carousel-slide');
                for (var i = 0; i < slides.length; i++) {
                    slides[i].style.cursor = 'pointer';
                    slides[i].addEventListener('click', function() {
                        var idx = parseInt(this.getAttribute('data-promo-idx'), 10);
                        openFullscreenViewer(idx);
                    });
                }
            }

            if (text) {
                var textEl = document.getElementById('promotion-text');
                textEl.textContent = text;
                textEl.style.display = '';
            }

            initCarousels();
        }).catch(function() {});
    }

    function showActivityPage() {
        document.getElementById('notice-overlay').style.display = 'none';
        loadActivities();
    }

    function showNoticeModal(content) {
        document.getElementById('notice-content').textContent = content;
        var overlay = document.getElementById('notice-overlay');
        overlay.style.display = 'flex';

        var countdown = 10;
        var countdownEl = document.getElementById('notice-countdown');
        var checkbox = document.getElementById('notice-checkbox');
        var checkboxLabel = document.getElementById('notice-checkbox-label');
        var confirmBtn = document.getElementById('notice-confirm-btn');

        var timer = setInterval(function() {
            countdown--;
            countdownEl.textContent = countdown;
            if (countdown <= 0) {
                clearInterval(timer);
                checkbox.disabled = false;
                checkboxLabel.style.cursor = '';
                checkboxLabel.innerHTML = '已完成阅读并自愿遵守活动规则';
            }
        }, 1000);

        checkbox.addEventListener('change', function() {
            if (checkbox.checked) {
                confirmBtn.disabled = false;
                confirmBtn.style.background = '#c53030';
                confirmBtn.style.color = '#fff';
                confirmBtn.style.cursor = 'pointer';
            } else {
                confirmBtn.disabled = true;
                confirmBtn.style.background = '#cbd5e0';
                confirmBtn.style.color = '#a0aec0';
                confirmBtn.style.cursor = 'not-allowed';
            }
        });

        confirmBtn.addEventListener('click', function() {
            if (checkbox.checked) {
                sessionStorage.setItem('notice_confirmed', '1');
                overlay.style.display = 'none';
                showActivityPage();
            }
        });
    }

    function loadNotice() {
        App.apiGet('/api/public/activity/notice').then(function(res) {
            if (res.data.code !== 0) { showActivityPage(); return; }
            var content = (res.data.data && res.data.data.content) || '';
            if (!content) { showActivityPage(); return; }
            if (sessionStorage.getItem('notice_confirmed')) { showActivityPage(); return; }
            showNoticeModal(content);
        }).catch(function() { showActivityPage(); });
    }

    function init() {
        loadPromotion();
        loadNotice();
        loadAboutUs();
        checkDirectSignup();
    }

    function loadActivities() {
        _currentPage = 1;
        _hasMore = false;
        _activities = [];
        App.apiGet('/api/public/activity/list?page=1&limit=' + _pageSize).then(function(res) {
            var loadingEl = document.getElementById('loading');
            if (loadingEl) {
                loadingEl.style.display = 'none';
            }
            if (res.data.code !== 0) {
                App.showToast('加载失败', 'error');
                return;
            }
            var data = res.data.data || {};
            var list = data.activities || [];
            _totalCount = data.total_count || 0;
            _hasMore = data.has_more || false;
            _activities = list;
            if (list.length === 0) {
                document.getElementById('empty-tip').style.display = 'block';
                return;
            }
            renderActivityList(list);
            updateLoadMoreUI();
            applyDirectSignup();
        }).catch(function() {
            var loadingEl = document.getElementById('loading');
            if (loadingEl) {
                loadingEl.style.display = 'none';
            }
            App.showToast('网络错误', 'error');
        });
    }

    function loadMoreActivities() {
        if (_isLoadingMore || !_hasMore) { return; }
        _isLoadingMore = true;
        _currentPage++;
        App.apiGet('/api/public/activity/list?page=' + _currentPage + '&limit=' + _pageSize).then(function(res) {
            _isLoadingMore = false;
            if (res.data.code !== 0) {
                _currentPage--;
                return;
            }
            var data = res.data.data || {};
            var list = data.activities || [];
            _hasMore = data.has_more || false;
            _activities = _activities.concat(list);
            appendActivityList(list);
            updateLoadMoreUI();
        }).catch(function() {
            _isLoadingMore = false;
            _currentPage--;
        });
    }

    window.openDescModal = function(activityId) {
        var act = null;
        for (var i = 0; i < _activities.length; i++) {
            if (_activities[i].id === activityId) {
                act = _activities[i];
                break;
            }
        }
        if (!act || !act.description) { return; }
        document.getElementById('desc-modal-title').textContent = act.title;
        document.getElementById('desc-modal-body').textContent = act.description;
        document.getElementById('desc-modal').style.display = 'flex';
    };

    window.closeDescModal = function() {
        document.getElementById('desc-modal').style.display = 'none';
    };

    function renderActivityList(list) {
        var container = document.getElementById('activity-list');
        var html = '';
        var now = new Date();

        for (var i = 0; i < list.length; i++) {
            var act = list[i];
            var isFull = act.capacity > 0 && act.signup_count >= act.capacity;
            var deadline = act.signup_deadline ? new Date(act.signup_deadline.replace(/-/g, '/')) : null;
            var isEnded = deadline && deadline < now;
            var canSignup = !isFull && !isEnded;

            var statusClass = 'status-open';
            var statusText = '可报名';
            if (isFull) {
                statusClass = 'status-full';
                statusText = '已满员';
            } else if (isEnded) {
                statusClass = 'status-ended';
                statusText = '已截止';
            }

            var countText = '已报名 ' + act.signup_count + ' 人';
            if (act.capacity > 0) {
                countText += ' / ' + act.capacity + ' 人';
            }

            html += '<div class="activity-card">';
            var coverImages = act.cover_images || [];
            if (coverImages.length > 1) {
                html += '<div class="carousel" data-index="0" data-count="' + coverImages.length + '">';
                html += '<div class="carousel-track">';
                for (var j = 0; j < coverImages.length; j++) {
                    html += '<img class="carousel-slide" src="' + _esc(coverImages[j].path) + '" alt="' + _esc(act.title) + '" loading="lazy">';
                }
                html += '</div>';
                html += '<div class="carousel-dots">';
                for (var j = 0; j < coverImages.length; j++) {
                    html += '<span class="carousel-dot' + (j === 0 ? ' active' : '') + '" data-index="' + j + '"></span>';
                }
                html += '</div></div>';
            } else if (coverImages.length === 1) {
                html += '<img class="activity-cover" src="' + _esc(coverImages[0].path) + '" alt="' + _esc(act.title) + '" loading="lazy">';
            } else if (act.cover_image) {
                html += '<img class="activity-cover" src="' + _esc(act.cover_image) + '" alt="' + _esc(act.title) + '" loading="lazy">';
            }
            html += '<div class="activity-body">';
            html += '<div class="activity-title">' + _esc(act.title) + '</div>';
            if (act.description) {
                html += '<div class="activity-desc" onclick="openDescModal(' + act.id + ')">' + _esc(act.description) + '</div>';
            }
            html += '<div class="activity-meta">';
            html += '<span>' + _esc(act.start_time) + ' ~ ' + _esc(act.end_time) + '</span>';
            if (act.signup_deadline) {
                html += '<span>截止: ' + _esc(act.signup_deadline) + '</span>';
            }
            html += '</div>';
            html += '<div class="activity-footer">';
            html += '<span class="activity-count">' + countText + '</span>';
            html += '<span class="signup-status ' + statusClass + '">' + statusText + '</span>';
            html += '</div>';

            if (canSignup) {
                var btnText = '立即报名';
                var btnClass = 'btn-signup';
                if (act.min_group_size > 1) {
                    var gt = act.group_type || 0;
                    if (gt === 0) { gt = 1; }
                    if (gt === 2) {
                        btnText = '拼团登记(' + act.min_group_size + '人)';
                        btnClass = 'btn-signup btn-group';
                    } else {
                        btnText = '拼团报名(' + act.min_group_size + '人)';
                        btnClass = 'btn-signup btn-group';
                    }
                }
                html += '<button class="' + btnClass + '" onclick="openSignupModal(' + act.id + ')">' + btnText + '</button>';
            } else {
                html += '<button class="btn-signup" disabled>' + statusText + '</button>';
            }

            html += '</div></div>';
        }

        container.innerHTML = html;
        initCarousels();
    }

    function appendActivityList(list) {
        var container = document.getElementById('activity-list');
        var loadMoreEl = document.getElementById('load-more-container');
        var now = new Date();
        var html = '';

        for (var i = 0; i < list.length; i++) {
            var act = list[i];
            var isFull = act.capacity > 0 && act.signup_count >= act.capacity;
            var deadline = act.signup_deadline ? new Date(act.signup_deadline.replace(/-/g, '/')) : null;
            var isEnded = deadline && deadline < now;
            var canSignup = !isFull && !isEnded;

            var statusClass = 'status-open';
            var statusText = '可报名';
            if (isFull) { statusClass = 'status-full'; statusText = '已满员'; }
            else if (isEnded) { statusClass = 'status-ended'; statusText = '已截止'; }

            var countText = '已报名 ' + act.signup_count + ' 人';
            if (act.capacity > 0) { countText += ' / ' + act.capacity + ' 人'; }

            html += '<div class="activity-card">';
            var coverImages = act.cover_images || [];
            if (coverImages.length > 1) {
                html += '<div class="carousel" data-index="0" data-count="' + coverImages.length + '">';
                html += '<div class="carousel-track">';
                for (var j = 0; j < coverImages.length; j++) {
                    html += '<img class="carousel-slide" src="' + _esc(coverImages[j].path) + '" alt="' + _esc(act.title) + '" loading="lazy">';
                }
                html += '</div>';
                html += '<div class="carousel-dots">';
                for (var j = 0; j < coverImages.length; j++) {
                    html += '<span class="carousel-dot' + (j === 0 ? ' active' : '') + '" data-index="' + j + '"></span>';
                }
                html += '</div></div>';
            } else if (coverImages.length === 1) {
                html += '<img class="activity-cover" src="' + _esc(coverImages[0].path) + '" alt="' + _esc(act.title) + '" loading="lazy">';
            } else if (act.cover_image) {
                html += '<img class="activity-cover" src="' + _esc(act.cover_image) + '" alt="' + _esc(act.title) + '" loading="lazy">';
            }
            html += '<div class="activity-body">';
            html += '<div class="activity-title">' + _esc(act.title) + '</div>';
            if (act.description) {
                html += '<div class="activity-desc" onclick="openDescModal(' + act.id + ')">' + _esc(act.description) + '</div>';
            }
            html += '<div class="activity-meta">';
            html += '<div class="activity-meta-row">';
            html += '<span class="signup-count ' + statusClass + '">' + countText + '</span>';
            html += '<span class="signup-status ' + statusClass + '">' + statusText + '</span>';
            html += '</div>';
            if (act.start_time) {
                html += '<div class="activity-meta-row">';
                html += '<span>时间: ' + _esc(act.start_time);
                if (act.end_time) { html += ' ~ ' + _esc(act.end_time); }
                html += '</span>';
                html += '</div>';
            }
            if (act.signup_deadline) {
                html += '<div class="activity-meta-row"><span>截止: ' + _esc(act.signup_deadline) + '</span></div>';
            }
            html += '</div>';
            if (canSignup) {
                if (act.min_group_size > 1) {
                    var gt = act.group_type || 0;
                    if (gt === 0) { gt = 1; }
                    if (gt === 2) {
                        html += '<button class="btn-signup btn-group" onclick="openSignupModal(' + act.id + ')">拼团登记(' + act.min_group_size + '人)</button>';
                    } else {
                        html += '<button class="btn-signup btn-group" onclick="openSignupModal(' + act.id + ')">拼团报名(' + act.min_group_size + '人)</button>';
                    }
                } else {
                    html += '<button class="btn-signup" onclick="openSignupModal(' + act.id + ')">立即报名</button>';
                }
            } else {
                html += '<button class="btn-signup" disabled>' + statusText + '</button>';
            }
            html += '</div></div>';
        }

        if (loadMoreEl) {
            loadMoreEl.insertAdjacentHTML('beforebegin', html);
        } else {
            container.insertAdjacentHTML('beforeend', html);
        }
        initCarousels();
    }

    function updateLoadMoreUI() {
        var el = document.getElementById('load-more-container');
        if (!el) { return; }
        if (_hasMore) {
            el.style.display = 'block';
            el.innerHTML = '<button class="btn-load-more" onclick="loadMoreActivities()">加载更多</button>';
        } else if (_activities.length > 0) {
            el.style.display = 'block';
            el.innerHTML = '<div class="load-more-tip">已加载全部 ' + _totalCount + ' 个活动</div>';
        } else {
            el.style.display = 'none';
        }
    }

    window.loadMoreActivities = loadMoreActivities;

    function initCarousels() {
        var carousels = document.querySelectorAll('.carousel');
        for (var i = 0; i < carousels.length; i++) {
            if (carousels[i].getAttribute('data-initialized')) { continue; }
            carousels[i].setAttribute('data-initialized', '1');
            (function(carousel) {
                var track = carousel.querySelector('.carousel-track');
                var dots = carousel.querySelectorAll('.carousel-dot');
                var count = parseInt(carousel.getAttribute('data-count'));
                var current = 0;
                var autoplayTimer = null;
                var isSnapping = false;

                if (count > 1) {
                    var firstClone = track.children[0].cloneNode(true);
                    track.appendChild(firstClone);
                }

                function setTrackPosition(idx, animate) {
                    if (animate === false) {
                        track.style.transition = 'none';
                    } else {
                        track.style.transition = 'transform 0.3s ease';
                    }
                    track.style.transform = 'translateX(-' + (idx * 100) + '%)';
                    if (animate === false) {
                        void track.offsetHeight;
                    }
                }

                function updateDots() {
                    var dotIdx = current >= count ? 0 : current;
                    for (var d = 0; d < dots.length; d++) {
                        dots[d].classList.toggle('active', d === dotIdx);
                    }
                }

                function goTo(idx) {
                    if (idx < 0) {
                        current = count - 1;
                        setTrackPosition(current);
                    } else {
                        current = idx;
                        setTrackPosition(current);
                    }
                    carousel.setAttribute('data-index', current >= count ? 0 : current);
                    updateDots();
                }

                if (count > 1) {
                    track.addEventListener('transitionend', function() {
                        if (current >= count && !isSnapping) {
                            isSnapping = true;
                            current = 0;
                            setTrackPosition(0, false);
                            carousel.setAttribute('data-index', 0);
                            updateDots();
                            isSnapping = false;
                        }
                    });
                }

                for (var d = 0; d < dots.length; d++) {
                    dots[d].addEventListener('click', function(e) {
                        var idx = parseInt(this.getAttribute('data-index'));
                        goTo(idx);
                        resetAutoplay();
                    });
                }

                var touchStartX = 0;
                var touchDeltaX = 0;
                carousel.addEventListener('touchstart', function(e) {
                    touchStartX = e.touches[0].clientX;
                    touchDeltaX = 0;
                }, { passive: true });
                carousel.addEventListener('touchmove', function(e) {
                    touchDeltaX = e.touches[0].clientX - touchStartX;
                }, { passive: true });
                carousel.addEventListener('touchend', function() {
                    if (Math.abs(touchDeltaX) > 40) {
                        goTo(current + (touchDeltaX < 0 ? 1 : -1));
                        resetAutoplay();
                    }
                });

                var mouseDown = false;
                var mouseStartX = 0;
                carousel.addEventListener('mousedown', function(e) {
                    mouseDown = true;
                    mouseStartX = e.clientX;
                    e.preventDefault();
                });
                carousel.addEventListener('mousemove', function(e) {
                    if (!mouseDown) { return; }
                    touchDeltaX = e.clientX - mouseStartX;
                });
                carousel.addEventListener('mouseup', function() {
                    if (!mouseDown) { return; }
                    mouseDown = false;
                    if (Math.abs(touchDeltaX) > 40) {
                        goTo(current + (touchDeltaX < 0 ? 1 : -1));
                        resetAutoplay();
                    }
                    touchDeltaX = 0;
                });
                carousel.addEventListener('mouseleave', function() {
                    mouseDown = false;
                });

                function startAutoplay() {
                    if (count <= 1) { return; }
                    autoplayTimer = setInterval(function() {
                        goTo(current + 1);
                    }, 3000);
                }
                function resetAutoplay() {
                    if (autoplayTimer) { clearInterval(autoplayTimer); }
                    startAutoplay();
                }
                startAutoplay();
            })(carousels[i]);
        }
    }

    function openFullscreenViewer(startIdx) {
        if (!_promotionImages.length) { return; }
        var viewer = document.getElementById('fullscreen-viewer');
        var track = document.getElementById('fullscreen-track');
        var dotsEl = document.getElementById('fullscreen-dots');
        var count = _promotionImages.length;
        var current = startIdx;
        var wasDragging = false;

        var html = '';
        for (var i = 0; i < count; i++) {
            html += '<div style="min-width:100vw; height:100vh; display:flex; align-items:center; justify-content:center;">';
            html += '<img src="' + _esc(_promotionImages[i].path) + '" style="max-width:100%; max-height:100vh; object-fit:contain;">';
            html += '</div>';
        }
        track.innerHTML = html;

        var dotsHtml = '';
        for (var i = 0; i < count; i++) {
            dotsHtml += '<span style="width:8px; height:8px; border-radius:50%; background:' + (i === current ? '#fff' : 'rgba(255,255,255,0.4)') + '; transition:background 0.2s;"></span>';
        }
        dotsEl.innerHTML = dotsHtml;

        function goTo(idx) {
            if (idx < 0) { idx = count - 1; }
            if (idx >= count) { idx = 0; }
            current = idx;
            track.style.transform = 'translateX(-' + (current * 100) + 'vw)';
            var dots = dotsEl.children;
            for (var d = 0; d < dots.length; d++) {
                dots[d].style.background = d === current ? '#fff' : 'rgba(255,255,255,0.4)';
            }
        }
        goTo(current);

        var dragStartX = 0;
        var dragDeltaX = 0;

        function onDragStart(x) {
            dragStartX = x;
            dragDeltaX = 0;
            wasDragging = false;
            track.style.transition = 'none';
        }
        function onDragMove(x) {
            dragDeltaX = x - dragStartX;
            if (Math.abs(dragDeltaX) > 10) { wasDragging = true; }
            var base = -(current * 100);
            var pct = (dragDeltaX / window.innerWidth) * 100;
            track.style.transform = 'translateX(' + (base + pct) + 'vw)';
        }
        function onDragEnd() {
            track.style.transition = 'transform 0.3s ease';
            if (Math.abs(dragDeltaX) > 50) {
                goTo(current + (dragDeltaX < 0 ? 1 : -1));
            } else {
                goTo(current);
            }
            dragDeltaX = 0;
        }

        viewer.ontouchstart = function(e) { onDragStart(e.touches[0].clientX); };
        viewer.ontouchmove = function(e) { onDragMove(e.touches[0].clientX); };
        viewer.ontouchend = function() { onDragEnd(); };

        var mouseDown = false;
        viewer.onmousedown = function(e) {
            mouseDown = true;
            onDragStart(e.clientX);
        };
        viewer.onmousemove = function(e) {
            if (!mouseDown) { return; }
            onDragMove(e.clientX);
        };
        viewer.onmouseup = function() {
            if (!mouseDown) { return; }
            mouseDown = false;
            onDragEnd();
        };

        viewer.onclick = function() {
            if (!wasDragging) {
                closeFullscreenViewer();
            }
        };

        viewer.style.display = 'block';
        document.body.style.overflow = 'hidden';
    }

    function closeFullscreenViewer() {
        var viewer = document.getElementById('fullscreen-viewer');
        viewer.style.display = 'none';
        viewer.ontouchstart = null;
        viewer.ontouchmove = null;
        viewer.ontouchend = null;
        viewer.onmousedown = null;
        viewer.onmousemove = null;
        viewer.onmouseup = null;
        viewer.onclick = null;
        document.body.style.overflow = '';
    }

    /* ====== Group signup helpers ====== */

    function parseTimeToEpoch(s) {
        if (!s) { return 0; }
        return new Date(s.replace(/-/g, '/')).getTime() / 1000;
    }

    function formatCountdown(seconds) {
        if (seconds < 0) { seconds = 0; }
        var m = Math.floor(seconds / 60);
        var s = seconds % 60;
        return m + ':' + (s < 10 ? '0' : '') + s;
    }

    function stopPolling() {
        if (_pollTimer) {
            clearInterval(_pollTimer);
            _pollTimer = null;
        }
    }

    function renderGroupWaiting(data) {
        var progressEl = document.getElementById('waiting-progress');
        var codeEl = document.getElementById('waiting-invite-code');
        var qrcodeEl = document.getElementById('waiting-qrcode');
        var membersEl = document.getElementById('waiting-members');
        var confirmBtn = document.getElementById('waiting-confirm-btn');

        progressEl.textContent = data.current_count + '/' + data.target_count + ' 人已报名';
        var inviteCode = (data.invite_code || '').trim();
        codeEl.textContent = inviteCode;

        /* QR code: encode current page URL with invite code */
        qrcodeEl.innerHTML = '';
        if (inviteCode && typeof QRCode !== 'undefined') {
            var url = window.location.origin + '/activity?invite=' + encodeURIComponent(inviteCode) + '&aid=' + _currentActivityId;
            new QRCode(qrcodeEl, {
                text: url,
                width: 160,
                height: 160,
                colorDark: '#000000',
                colorLight: '#ffffff'
            });
        }

        /* Members list */
        var members = data.members || [];
        var mhtml = '';
        for (var i = 0; i < members.length; i++) {
            mhtml += '<div class="waiting-member">';
            mhtml += '<span class="member-name">' + _esc(members[i].name) + '</span>';
            if (members[i].is_leader) {
                mhtml += '<span class="member-badge">团长</span>';
            }
            mhtml += '</div>';
        }
        membersEl.innerHTML = mhtml;

        /* Show confirm button only for leader when group is full (type=2 only) */
        if (_groupType !== 1 && _isLeader && data.current_count >= data.target_count) {
            confirmBtn.style.display = '';
        } else {
            confirmBtn.style.display = 'none';
        }
    }

    function startGroupPolling() {
        stopPolling();
        pollGroupStatus();
        _pollTimer = setInterval(pollGroupStatus, 3000);
    }

    function pollGroupStatus() {
        if (_groupType === 1) {
            if (!_inviteCode) { return; }
            var url = '/api/public/activity/group_status?invite_code=' + encodeURIComponent(_inviteCode)
                + '&activity_id=' + _currentActivityId
                + '&group_type=1'
                + '&name=' + encodeURIComponent(_myName)
                + '&phone=' + encodeURIComponent(_myPhone);
            App.apiGet(url).then(function(res) {
                if (res.data.code === 1119) {
                    stopPolling();
                    App.showToast('拼团已过期或已结束', 'error');
                    document.getElementById('group-waiting').style.display = 'none';
                    document.getElementById('signup-form').style.display = 'block';
                    document.getElementById('signup-btn').disabled = false;
                    return;
                }
                if (res.data.code !== 0) {
                    stopPolling();
                    App.showToast(res.data.message || '获取状态失败', 'error');
                    return;
                }
                var data = res.data.data;
                if (data.status === 1) {
                    stopPolling();
                    showGroupConfirmed(data);
                    return;
                }
                if (data.status === 2) {
                    stopPolling();
                    App.showToast('拼团已取消或已过期', 'error');
                    document.getElementById('group-waiting').style.display = 'none';
                    document.getElementById('signup-form').style.display = 'block';
                    document.getElementById('signup-btn').disabled = false;
                    return;
                }
                var displayData = {
                    current_count: data.current_count,
                    target_count: data.target_count,
                    invite_code: _inviteCode,
                    members: data.members
                };
                renderGroupWaiting(displayData);
            }).catch(function() {});
            return;
        }

        if (!_groupId) { return; }
        App.apiGet('/api/public/activity/group_status?group_id=' + _groupId).then(function(res) {
            if (res.data.code !== 0) {
                stopPolling();
                App.showToast(res.data.message || '获取状态失败', 'error');
                return;
            }
            var data = res.data.data;

            /* status: 0=waiting, 1=confirmed, 2=cancelled */
            if (data.status === 2) {
                stopPolling();
                var reason = data.cancel_reason || 0;
                var msg = '拼团已取消';
                if (reason === 3) { msg = '拼团已超时，报名失败'; }
                else if (reason === 2) { msg = '团长已离开，拼团报名失败'; }
                else if (reason === 1) { msg = '团长已解散该拼团，报名失败'; }
                App.showToast(msg, 'error');
                document.getElementById('group-waiting').style.display = 'none';
                document.getElementById('signup-form').style.display = 'block';
                document.getElementById('signup-btn').disabled = false;
                return;
            }

            if (data.status === 1) {
                stopPolling();
                showGroupConfirmed(data);
                return;
            }

            /* Still waiting - update UI with combined data */
            var displayData = {
                current_count: data.current_count,
                target_count: data.target_count,
                invite_code: _currentInviteCode,
                members: data.members
            };
            renderGroupWaiting(displayData);
        }).catch(function() {});
    }

    var _currentInviteCode = '';

    function showGroupWaiting(data) {
        _groupId = data.group_id;
        _isLeader = data.is_leader;
        _currentInviteCode = (data.invite_code || '').trim();
        _createdAtEpoch = parseTimeToEpoch(data.created_at);
        _inviteCode = _currentInviteCode;
        _targetCount = data.target_count || 0;

        var act = null;
        for (var i = 0; i < _activities.length; i++) {
            if (_activities[i].id === _currentActivityId) { act = _activities[i]; break; }
        }
        _activityTitle = act ? act.title : '';

        document.getElementById('signup-form').style.display = 'none';
        document.getElementById('signup-success').style.display = 'none';
        document.getElementById('group-waiting').style.display = 'block';

        var leaderWarning = document.getElementById('leader-warning');
        var shareSection = document.getElementById('session-share-section');
        var confirmBtn = document.getElementById('waiting-confirm-btn');

        if (_groupType === 1) {
            leaderWarning.style.display = 'none';
            shareSection.style.display = '';
            confirmBtn.style.display = 'none';

            var groupImage = (data && data.group_image) || '';
            var waitingImageSection = document.getElementById('waiting-group-image-section');
            if (groupImage) {
                document.getElementById('waiting-group-image').src = groupImage;
                waitingImageSection.style.display = '';
            } else {
                waitingImageSection.style.display = 'none';
            }
        } else {
            if (_isLeader) { leaderWarning.style.display = ''; }
            shareSection.style.display = 'none';
        }

        renderGroupWaiting(data);
        startGroupPolling();
    }

    function showGroupConfirmed(data) {
        stopPolling();
        document.getElementById('group-waiting').style.display = 'none';
        document.getElementById('signup-success').style.display = 'block';

        var groupImage = (data && data.group_image) || '';
        if (groupImage) {
            document.getElementById('group-image').src = groupImage;
            document.getElementById('group-image-section').style.display = 'block';
        } else {
            document.getElementById('group-image-section').style.display = 'none';
        }

        loadActivities();
        _isGroupMode = false;
    }

    function sendLeaveBeacon() {
        if (!_groupId || !_myName || !_myPhone) { return; }
        if (_groupType === 1) { return; }
        var payload = JSON.stringify({
            group_id: _groupId,
            name: _myName,
            phone: _myPhone
        });
        var blob = new Blob([payload], { type: 'application/json' });
        navigator.sendBeacon('/api/public/activity/leave_group', blob);
    }

    /* ====== Modal open/close ====== */

    window.openSignupModal = function(activityId) {
        _currentActivityId = activityId;
        var act = null;
        for (var i = 0; i < _activities.length; i++) {
            if (_activities[i].id === activityId) {
                act = _activities[i];
                break;
            }
        }

        var effectiveGroupType = act.group_type || 0;
        if (effectiveGroupType === 0 && act.min_group_size > 1) { effectiveGroupType = 1; }
        _groupType = effectiveGroupType;
        _isGroupMode = effectiveGroupType > 0;

        document.getElementById('signup-activity-title').textContent = act ? act.title : '活动报名';
        document.getElementById('signup-name').value = '';
        document.getElementById('signup-phone').value = '';
        document.getElementById('signup-grade').value = '';
        document.getElementById('signup-type').value = '';
        document.getElementById('signup-error').textContent = '';
        document.getElementById('signup-form').style.display = 'block';
        document.getElementById('signup-success').style.display = 'none';
        document.getElementById('group-waiting').style.display = 'none';
        document.getElementById('signup-btn').disabled = false;
        document.getElementById('signup-modal').style.display = 'flex';

        var inviteGroup = document.getElementById('invite-code-group');
        var leaderWarning = document.getElementById('leader-warning');
        var batchArea = document.getElementById('batch-members-area');
        var nameGroup = document.getElementById('signup-name').parentNode;
        var phoneGroup = document.getElementById('signup-phone').parentNode;
        var gradeGroup = document.getElementById('signup-grade').parentNode;
        var typeGroup = document.getElementById('signup-type').parentNode;

        inviteGroup.style.display = 'none';
        leaderWarning.style.display = 'none';
        batchArea.style.display = 'none';
        nameGroup.style.display = '';
        phoneGroup.style.display = '';
        gradeGroup.style.display = '';
        typeGroup.style.display = '';

        if (_groupType === 1) {
            inviteGroup.style.display = '';
            leaderWarning.style.display = 'none';
            document.getElementById('signup-invite-code').value = '';

            var params = new URLSearchParams(window.location.search);
            var inviteParam = params.get('invite');
            var aidParam = parseInt(params.get('aid'));
            if (inviteParam && aidParam === activityId) {
                document.getElementById('signup-invite-code').value = inviteParam.trim().toUpperCase();
            }
        } else if (_groupType === 2) {
            nameGroup.style.display = 'none';
            phoneGroup.style.display = 'none';
            gradeGroup.style.display = 'none';
            typeGroup.style.display = 'none';
            batchArea.style.display = '';
            document.getElementById('batch-count-label').textContent = act.min_group_size;
            renderBatchForm(act.min_group_size);
        }
    };

    function renderBatchForm(count) {
        var container = document.getElementById('batch-members-form');
        var html = '';
        for (var i = 0; i < count; i++) {
            var label = '第' + (i + 1) + '位学生';
            if (i === 0) { label += ' (团长)'; }
            html += '<div class="batch-member-group">';
            html += '<div class="batch-member-label">' + label + '</div>';
            html += '<div class="form-group">';
            html += '<input type="text" class="form-input batch-name" placeholder="姓名" maxlength="50" data-idx="' + i + '">';
            html += '</div>';
            html += '<div class="form-group">';
            html += '<input type="tel" class="form-input batch-phone" placeholder="手机号" maxlength="11" data-idx="' + i + '">';
            html += '</div>';
            html += '<div class="form-group">';
            html += '<select class="form-input batch-grade" data-idx="' + i + '">';
            html += '<option value="">请选择年级</option>';
            html += '<option value="一年级">一年级</option>';
            html += '<option value="二年级">二年级</option>';
            html += '<option value="三年级">三年级</option>';
            html += '<option value="四年级">四年级</option>';
            html += '<option value="五年级">五年级</option>';
            html += '<option value="六年级">六年级</option>';
            html += '<option value="七年级(初一)">七年级(初一)</option>';
            html += '<option value="八年级(初二)">八年级(初二)</option>';
            html += '<option value="九年级(初三)">九年级(初三)</option>';
            html += '</select>';
            html += '</div>';
            html += '<div class="form-group">';
            html += '<select class="form-input batch-type" data-idx="' + i + '">';
            html += '<option value="">请选择报名类型</option>';
            html += '<option value="全托">全托</option>';
            html += '<option value="晚托">晚托</option>';
            html += '<option value="午托">午托</option>';
            html += '</select>';
            html += '</div>';
            html += '</div>';
        }
        container.innerHTML = html;
    }

    window.closeSignupModal = function() {
        stopScan();
        stopPolling();
        if (_isGroupMode && _groupId > 0) {
            sendLeaveBeacon();
        }
        document.getElementById('signup-modal').style.display = 'none';
        document.getElementById('group-waiting').style.display = 'none';
        _currentActivityId = 0;
        _groupId = 0;
        _isLeader = false;
        _isGroupMode = false;
        _groupType = 0;
        _myName = '';
        _myPhone = '';
        _currentInviteCode = '';
        _createdAtEpoch = 0;
        _inviteCode = '';
        _activityTitle = '';
        _targetCount = 0;
    };

    window.submitSignup = function() {
        var errEl = document.getElementById('signup-error');
        errEl.textContent = '';

        if (_groupType === 2) {
            submitBatchGroupSignup();
            return;
        }

        var name = document.getElementById('signup-name').value.trim();
        var phone = document.getElementById('signup-phone').value.trim();
        var grade = document.getElementById('signup-grade').value;
        var signupType = document.getElementById('signup-type').value;

        if (!name) {
            errEl.textContent = '请输入姓名';
            return;
        }
        if (name.length > 50) {
            errEl.textContent = '姓名不能超过50个字符';
            return;
        }
        if (!/^1[0-9]{10}$/.test(phone)) {
            errEl.textContent = '请输入正确的11位手机号';
            return;
        }
        if (!signupType) {
            errEl.textContent = '请选择报名类型';
            return;
        }

        document.getElementById('signup-btn').disabled = true;

        if (_isGroupMode) {
            submitGroupSignup(name, phone, grade, signupType);
        } else {
            submitNormalSignup(name, phone, grade, signupType);
        }
    };

    function submitNormalSignup(name, phone, grade, signupType) {
        App.apiPost('/api/public/activity/signup', {
            activity_id: _currentActivityId,
            name: name,
            phone: phone,
            grade: grade,
            signup_type: signupType
        }).then(function(res) {
            if (res.data.code === 0) {
                document.getElementById('signup-form').style.display = 'none';
                document.getElementById('signup-success').style.display = 'block';

                var groupImage = (res.data.data && res.data.data.group_image) || '';
                if (groupImage) {
                    document.getElementById('group-image').src = groupImage;
                    document.getElementById('group-image-section').style.display = 'block';
                } else {
                    document.getElementById('group-image-section').style.display = 'none';
                }

                loadActivities();
            } else {
                errEl.textContent = res.data.message || '报名失败';
                document.getElementById('signup-btn').disabled = false;
            }
        }).catch(function() {
            errEl.textContent = '网络错误，请重试';
            document.getElementById('signup-btn').disabled = false;
        });
    }

    function submitGroupSignup(name, phone, grade, signupType) {
        var inviteCode = document.getElementById('signup-invite-code').value.trim().toUpperCase();

        _myName = name;
        _myPhone = phone;

        App.apiPost('/api/public/activity/group_signup', {
            activity_id: _currentActivityId,
            name: name,
            phone: phone,
            grade: grade,
            signup_type: signupType,
            invite_code: inviteCode
        }).then(function(res) {
            if (res.data.code === 0) {
                var data = res.data.data;
                if (data.auto_confirmed) {
                    showGroupConfirmed(data);
                } else {
                    showGroupWaiting(data);
                }
                loadActivities();
            } else if (res.data.code === 1119) {
                var errEl = document.getElementById('signup-error');
                errEl.textContent = '邀请码无效或拼团已结束';
                document.getElementById('signup-btn').disabled = false;
            } else if (res.data.code === 1120) {
                var errEl = document.getElementById('signup-error');
                errEl.textContent = '活动容量已满，无法容纳拼团';
                document.getElementById('signup-btn').disabled = false;
            } else {
                var errEl = document.getElementById('signup-error');
                errEl.textContent = res.data.message || '报名失败';
                document.getElementById('signup-btn').disabled = false;
            }
        }).catch(function() {
            var errEl = document.getElementById('signup-error');
            errEl.textContent = '网络错误，请重试';
            document.getElementById('signup-btn').disabled = false;
        });
    }

    function submitBatchGroupSignup() {
        var errEl = document.getElementById('signup-error');
        var nameInputs = document.querySelectorAll('#batch-members-form .batch-name');
        var phoneInputs = document.querySelectorAll('#batch-members-form .batch-phone');
        var gradeInputs = document.querySelectorAll('#batch-members-form .batch-grade');
        var typeInputs = document.querySelectorAll('#batch-members-form .batch-type');
        var members = [];

        for (var i = 0; i < nameInputs.length; i++) {
            var name = nameInputs[i].value.trim();
            var phone = phoneInputs[i].value.trim();
            var grade = gradeInputs[i].value;
            var signupType = typeInputs[i].value;

            if (!name) {
                errEl.textContent = '请输入第' + (i + 1) + '位学生的姓名';
                return;
            }
            if (name.length > 50) {
                errEl.textContent = '第' + (i + 1) + '位学生姓名不能超过50个字符';
                return;
            }
            if (!/^1[0-9]{10}$/.test(phone)) {
                errEl.textContent = '请输入第' + (i + 1) + '位学生的正确11位手机号';
                return;
            }
            if (!signupType) {
                errEl.textContent = '请选择第' + (i + 1) + '位学生的报名类型';
                return;
            }

            members.push({
                name: name,
                phone: phone,
                grade: grade,
                signup_type: signupType
            });
        }

        document.getElementById('signup-btn').disabled = true;

        App.apiPost('/api/public/activity/batch_group_signup', {
            activity_id: _currentActivityId,
            members: members
        }).then(function(res) {
            if (res.data.code === 0) {
                document.getElementById('signup-form').style.display = 'none';
                document.getElementById('signup-success').style.display = 'block';

                var groupImage = (res.data.data && res.data.data.group_image) || '';
                if (groupImage) {
                    document.getElementById('group-image').src = groupImage;
                    document.getElementById('group-image-section').style.display = 'block';
                } else {
                    document.getElementById('group-image-section').style.display = 'none';
                }

                loadActivities();
            } else {
                errEl.textContent = res.data.message || '报名失败';
                document.getElementById('signup-btn').disabled = false;
            }
        }).catch(function() {
            errEl.textContent = '网络错误，请重试';
            document.getElementById('signup-btn').disabled = false;
        });
    }

    /* ====== Group actions ====== */

    window.confirmGroup = function() {
        if (!_groupId || !_myName || !_myPhone) { return; }
        App.apiPost('/api/public/activity/confirm_group', {
            group_id: _groupId,
            name: _myName,
            phone: _myPhone
        }).then(function(res) {
            if (res.data.code === 0) {
                showGroupConfirmed(res.data.data);
            } else {
                App.showToast(res.data.message || '确认失败', 'error');
            }
        }).catch(function() {
            App.showToast('网络错误，请重试', 'error');
        });
    };

    window.cancelGroup = function() {
        if (_groupType === 1) {
            if (!_inviteCode || !_myName || !_myPhone) { return; }
            if (!confirm('确定要取消拼团吗？')) { return; }
            App.apiPost('/api/public/activity/cancel_group', {
                invite_code: _inviteCode,
                activity_id: _currentActivityId,
                name: _myName,
                phone: _myPhone,
                group_type: 1
            }).then(function(res) {
                if (res.data.code === 0) {
                    stopPolling();
                    App.showToast('拼团已取消', 'info');
                    document.getElementById('group-waiting').style.display = 'none';
                    document.getElementById('signup-form').style.display = 'block';
                    document.getElementById('signup-btn').disabled = false;
                    _inviteCode = '';
                    _isGroupMode = _groupType > 0;
                    loadActivities();
                } else {
                    App.showToast(res.data.message || '取消失败', 'error');
                }
            }).catch(function() {
                App.showToast('网络错误，请重试', 'error');
            });
            return;
        }

        if (!_groupId || !_myName || !_myPhone) { return; }
        if (!confirm('确定要取消拼团吗？')) { return; }

        App.apiPost('/api/public/activity/cancel_group', {
            group_id: _groupId,
            name: _myName,
            phone: _myPhone
        }).then(function(res) {
            if (res.data.code === 0) {
                stopPolling();
                App.showToast('拼团已取消', 'info');
                document.getElementById('group-waiting').style.display = 'none';
                document.getElementById('signup-form').style.display = 'block';
                document.getElementById('signup-btn').disabled = false;
                _groupId = 0;
                _isGroupMode = _groupType > 0;
                loadActivities();
            } else {
                App.showToast(res.data.message || '取消失败', 'error');
            }
        }).catch(function() {
            App.showToast('网络错误，请重试', 'error');
        });
    };

    /* QR scanner state */
    var _scanStream = null;
    var _scanTimer = null;

    window.startScan = function() {
        if (typeof jsQR === 'undefined') {
            App.showToast('扫码组件未加载', 'error');
            return;
        }
        document.getElementById('scan-mode-sheet').style.display = 'block';
    };

    window.closeScanSheet = function() {
        document.getElementById('scan-mode-sheet').style.display = 'none';
    };

    window.pickCameraScan = function() {
        closeScanSheet();
        if (!window.isSecureContext || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
            App.showToast('实时扫描需要HTTPS安全连接，请使用"从相册选择"', 'error');
            return;
        }
        var overlay = document.getElementById('scanner-overlay');
        overlay.style.display = 'flex';

        navigator.mediaDevices.getUserMedia({
            video: { facingMode: 'environment' }
        }).then(function(stream) {
            _scanStream = stream;
            var video = document.getElementById('scanner-video');
            video.srcObject = stream;
            video.play();
            _scanTimer = setInterval(decodeFrame, 300);
        }).catch(function() {
            overlay.style.display = 'none';
            App.showToast('无法访问摄像头，请从相册选择', 'error');
        });
    };

    window.pickAlbumScan = function() {
        closeScanSheet();
        document.getElementById('qr-file-input').click();
    };

    window.handleQRImage = function(event) {
        var file = event.target.files[0];
        if (!file) { return; }
        if (typeof jsQR === 'undefined') {
            App.showToast('扫码组件未加载', 'error');
            return;
        }
        var reader = new FileReader();
        reader.onload = function(e) {
            var img = new Image();
            img.onload = function() {
                var canvas = document.getElementById('scanner-canvas');
                canvas.width = img.width;
                canvas.height = img.height;
                var ctx = canvas.getContext('2d');
                ctx.drawImage(img, 0, 0);
                var imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
                var code = jsQR(imageData.data, imageData.width, imageData.height);
                if (code && code.data) {
                    var result = code.data.trim().toUpperCase();
                    if (/^[2-9A-HJ-NP-Z]{6}$/.test(result)) {
                        document.getElementById('signup-invite-code').value = result;
                        App.showToast('邀请码已识别');
                    } else {
                        App.showToast('未识别到有效的邀请码', 'error');
                    }
                } else {
                    App.showToast('未识别到二维码，请重试', 'error');
                }
            };
            img.src = e.target.result;
        };
        reader.readAsDataURL(file);
        event.target.value = '';
    };

    function decodeFrame() {
        var video = document.getElementById('scanner-video');
        var canvas = document.getElementById('scanner-canvas');
        if (!video.videoWidth) { return; }
        canvas.width = video.videoWidth;
        canvas.height = video.videoHeight;
        var ctx = canvas.getContext('2d');
        ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
        var imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        var code = jsQR(imageData.data, imageData.width, imageData.height);
        if (code && code.data) {
            var result = code.data.trim().toUpperCase();
            if (/^[2-9A-HJ-NP-Z]{6}$/.test(result)) {
                document.getElementById('signup-invite-code').value = result;
                stopScan();
                App.showToast('邀请码已识别');
            }
        }
    }

    window.stopScan = function() {
        if (_scanTimer) {
            clearInterval(_scanTimer);
            _scanTimer = null;
        }
        if (_scanStream) {
            var tracks = _scanStream.getTracks();
            for (var i = 0; i < tracks.length; i++) {
                tracks[i].stop();
            }
            _scanStream = null;
        }
        var video = document.getElementById('scanner-video');
        video.srcObject = null;
        document.getElementById('scanner-overlay').style.display = 'none';
    };

    /* Leave group on page close */
    window.addEventListener('beforeunload', function() {
        if (_isGroupMode && _groupId > 0) {
            sendLeaveBeacon();
        }
    });

    /* Close modal on overlay click */
    function loadAboutUs() {
        App.apiGet('/api/public/activity/about_us').then(function(res) {
            if (res.data.code !== 0) { return; }
            var cards = [];
            if (res.data.data) {
                if (Array.isArray(res.data.data)) { cards = res.data.data; }
                else {
                    for (var k in res.data.data) {
                        if (res.data.data.hasOwnProperty(k)) { cards.push(res.data.data[k]); }
                    }
                }
            }
            if (cards.length === 0) { return; }
            cards.sort(function(a, b) { return (a.sort_order || 0) - (b.sort_order || 0); });

            var section = document.getElementById('about-us-section');
            section.style.display = '';
            var container = document.getElementById('about-us-cards');
            var html = '';
            for (var i = 0; i < cards.length; i++) {
                html += renderAboutUsCardHTML(cards[i]);
            }
            container.innerHTML = html;
        }).catch(function() {});
    }

    function renderAboutUsCardHTML(card) {
        var layout = card.layout_type || 1;
        var imgSrc = card.image_path || '';
        var text = _esc(card.text_content || '');
        var imgTag = imgSrc ? '<img src="' + imgSrc + '" alt="" loading="lazy">' : '';
        var textTag = text ? '<div class="auc-text">' + text.replace(/\n/g, '<br>') + '</div>' : '';
        var imgFull = imgSrc ? '<img src="' + imgSrc + '" class="auc-img-full" alt="" loading="lazy">' : '';
        var imgSmall = imgSrc ? '<img src="' + imgSrc + '" class="auc-img-small" alt="" loading="lazy">' : '';

        var inner = '';
        switch (layout) {
            case 1:
                inner = imgFull + textTag;
                break;
            case 2:
                inner = '<div class="auc-row">' +
                    '<div class="auc-half">' + imgTag + '</div>' +
                    '<div class="auc-half auc-pad">' + textTag + '</div>' +
                    '</div>';
                break;
            case 3:
                inner = '<div class="auc-row">' +
                    '<div class="auc-half auc-pad">' + textTag + '</div>' +
                    '<div class="auc-half">' + imgTag + '</div>' +
                    '</div>';
                break;
            case 4:
                inner = textTag + imgFull;
                break;
            case 5:
                inner = '<div class="auc-float-left">' + imgSmall + '</div>' + textTag;
                break;
            case 6:
                inner = '<div class="auc-float-right">' + imgSmall + '</div>' + textTag;
                break;
            case 7:
                inner = '<div class="auc-bg">' +
                    (imgSrc ? '<img src="' + imgSrc + '" class="auc-bg-img" alt="" loading="lazy">' : '') +
                    '<div class="auc-bg-text">' + text.replace(/\n/g, '<br>') + '</div>' +
                    '</div>';
                break;
            case 8:
                inner = '<div class="auc-center-img">' + imgSmall + '</div>' + textTag;
                break;
            case 9:
                inner = textTag + '<div class="auc-center-img">' + imgSmall + '</div>';
                break;
            default:
                inner = imgFull + textTag;
        }
        return '<div class="about-us-card" data-layout="' + layout + '">' + inner + '</div>';
    }

    /* ====== Sharing functions ====== */

    function getShareUrl() {
        return window.location.origin + '/activity?invite=' + encodeURIComponent(_inviteCode) + '&aid=' + _currentActivityId;
    }

    window.copyShareLink = function() {
        var text = _myName + ' 邀请您一起报名「' + _activityTitle + '」，还差 '
            + (_targetCount - 1) + ' 人即可成团！\n'
            + '点击链接加入: ' + getShareUrl();
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(text).then(function() {
                App.showToast('链接已复制');
            }).catch(function() {
                fallbackCopy(text);
            });
        } else {
            fallbackCopy(text);
        }
    };

    function fallbackCopy(text) {
        var ta = document.createElement('textarea');
        ta.value = text;
        ta.style.position = 'fixed';
        ta.style.left = '-9999px';
        document.body.appendChild(ta);
        ta.select();
        try {
            document.execCommand('copy');
            App.showToast('链接已复制');
        } catch (e) {
            App.showToast('复制失败，请手动复制', 'error');
        }
        document.body.removeChild(ta);
    }

    window.copyShareCard = function() {
        var canvas = document.getElementById('share-card-canvas');
        var ctx = canvas.getContext('2d');
        var w = 600, h = 900;

        ctx.fillStyle = '#FF6B35';
        ctx.fillRect(0, 0, w, h);

        ctx.fillStyle = '#ffffff';
        roundRect(ctx, 24, 24, w - 48, h - 48, 20, true, false);

        ctx.fillStyle = '#FF6B35';
        ctx.font = 'bold 36px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText('星芽教育', w / 2, 100);

        ctx.fillStyle = '#333333';
        ctx.font = '24px sans-serif';
        ctx.fillText('拼团邀请', w / 2, 160);

        ctx.fillStyle = '#555555';
        ctx.font = '20px sans-serif';
        var titleText = '「' + _activityTitle + '」';
        if (ctx.measureText(titleText).width > w - 100) {
            titleText = titleText.substring(0, 14) + '...」';
        }
        ctx.fillText(titleText, w / 2, 220);

        ctx.fillStyle = '#FF6B35';
        ctx.font = 'bold 28px sans-serif';
        ctx.fillText('还差 ' + (_targetCount - 1) + ' 人即可成团', w / 2, 280);

        ctx.fillStyle = '#333333';
        ctx.font = 'bold 72px monospace';
        ctx.fillText(_inviteCode, w / 2, 400);

        ctx.fillStyle = '#999999';
        ctx.font = '18px sans-serif';
        ctx.fillText('邀请码', w / 2, 440);

        var qrUrl = getShareUrl();
        if (typeof QRCode !== 'undefined') {
            var qrCanvas = document.createElement('canvas');
            var qrSize = 200;
            qrCanvas.width = qrSize;
            qrCanvas.height = qrSize;
            var qrDiv = document.createElement('div');
            qrDiv.style.position = 'absolute';
            qrDiv.style.left = '-9999px';
            document.body.appendChild(qrDiv);
            new QRCode(qrDiv, {
                text: qrUrl,
                width: qrSize,
                height: qrSize,
                colorDark: '#000000',
                colorLight: '#ffffff'
            });
            setTimeout(function() {
                var qrImg = qrDiv.querySelector('canvas') || qrDiv.querySelector('img');
                if (qrImg) {
                    ctx.drawImage(qrImg, (w - qrSize) / 2, 480, qrSize, qrSize);
                }
                document.body.removeChild(qrDiv);

                ctx.fillStyle = '#999999';
                ctx.font = '16px sans-serif';
                ctx.fillText('扫码立即加入拼团', w / 2, 720);

                ctx.fillStyle = '#cccccc';
                ctx.font = '14px sans-serif';
                ctx.fillText('复制链接分享给好友', w / 2, 820);

                showCardPreview();
            }, 200);
        } else {
            showCardPreview();
        }
    };

    function roundRect(ctx, x, y, w, h, r, fill, stroke) {
        ctx.beginPath();
        ctx.moveTo(x + r, y);
        ctx.arcTo(x + w, y, x + w, y + h, r);
        ctx.arcTo(x + w, y + h, x, y + h, r);
        ctx.arcTo(x, y + h, x, y, r);
        ctx.arcTo(x, y, x + w, y, r);
        ctx.closePath();
        if (fill) { ctx.fill(); }
        if (stroke) { ctx.stroke(); }
    }

    function showCardPreview() {
        document.getElementById('share-card-modal').style.display = 'flex';
    }

    window.closeCardPreview = function() {
        document.getElementById('share-card-modal').style.display = 'none';
    };

    window.copyCardImage = function() {
        var canvas = document.getElementById('share-card-canvas');
        canvas.toBlob(function(blob) {
            if (navigator.clipboard && navigator.clipboard.write) {
                var item = new ClipboardItem({ 'image/png': blob });
                navigator.clipboard.write([item]).then(function() {
                    App.showToast('卡片图片已复制');
                }).catch(function() {
                    App.showToast('请长按图片保存', 'error');
                });
            } else {
                App.showToast('请长按图片保存', 'error');
            }
        }, 'image/png');
    };

    /* ====== Direct signup from shared link ====== */

    var _directSignup = null; /* { activityId, inviteCode } */

    function checkDirectSignup() {
        var params = new URLSearchParams(window.location.search);
        var invite = params.get('invite');
        var aid = parseInt(params.get('aid'));
        if (!invite || !aid) { return; }

        window.history.replaceState({}, '', '/activity');
        _directSignup = { activityId: aid, inviteCode: invite.trim().toUpperCase() };
    }

    function applyDirectSignup() {
        if (!_directSignup) { return; }
        var ds = _directSignup;
        _directSignup = null;
        openSignupModal(ds.activityId);
        if (_groupType === 1) {
            document.getElementById('signup-invite-code').value = ds.inviteCode;
        }
    }

    document.getElementById('signup-modal').addEventListener('click', function(e) {
        if (e.target === this) {
            closeSignupModal();
        }
    });

    init();
})();
