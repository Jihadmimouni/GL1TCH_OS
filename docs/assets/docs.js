/*
 * Shared behavior for GL1TCH OS docs (every page under docs/).
 * Vanilla JS, no dependencies, degrades to plain static HTML if disabled:
 * every nav link and tab heading is a real element already present in the
 * markup - this only adds the toggle/highlight/copy affordances on top.
 */
(function () {
    "use strict";

    var root = document.documentElement;

    /* ---- theme toggle (persisted; falls back to prefers-color-scheme) ---- */

    function applyTheme(theme) {
        if (theme === "dark" || theme === "light") {
            root.setAttribute("data-theme", theme);
        } else {
            root.removeAttribute("data-theme");
        }
        var btn = document.getElementById("themeToggle");
        if (btn) {
            var isDark = theme === "dark" ||
                (!theme && window.matchMedia("(prefers-color-scheme: dark)").matches);
            btn.querySelector(".theme-label").textContent = isDark ? "Light mode" : "Dark mode";
            btn.querySelector(".theme-icon").textContent = isDark ? "☀" : "☽";
        }
    }

    var saved = null;
    try { saved = localStorage.getItem("gl1tch-docs-theme"); } catch (e) { /* private mode, etc. */ }
    applyTheme(saved);

    var themeBtn = document.getElementById("themeToggle");
    if (themeBtn) {
        themeBtn.addEventListener("click", function () {
            var current = root.getAttribute("data-theme");
            var isDark = current === "dark" ||
                (!current && window.matchMedia("(prefers-color-scheme: dark)").matches);
            var next = isDark ? "light" : "dark";
            applyTheme(next);
            try { localStorage.setItem("gl1tch-docs-theme", next); } catch (e) { /* ignore */ }
        });
    }

    /* ---- mobile sidebar toggle ---- */

    var shell = document.querySelector(".shell");
    var navToggle = document.getElementById("sidebarToggle");
    var navBackdrop = document.getElementById("sidebarBackdrop");

    function setNavOpen(open) {
        if (!shell) { return; }
        shell.classList.toggle("nav-open", open);
        if (navToggle) { navToggle.setAttribute("aria-expanded", open ? "true" : "false"); }
    }

    if (navToggle) {
        navToggle.addEventListener("click", function () {
            setNavOpen(!shell.classList.contains("nav-open"));
        });
    }
    if (navBackdrop) {
        navBackdrop.addEventListener("click", function () { setNavOpen(false); });
    }
    document.addEventListener("keydown", function (e) {
        if (e.key === "Escape") { setNavOpen(false); }
    });

    /* ---- tabs: each .tabs-group holds one or more .tab-panel[data-tab] --- */

    Array.prototype.forEach.call(document.querySelectorAll(".tabs-group"), function (group) {
        var panels = Array.prototype.slice.call(group.querySelectorAll(".tab-panel"));
        if (panels.length < 2) {
            /* nothing to switch between; show the single panel plainly */
            panels.forEach(function (p) { p.hidden = false; });
            return;
        }

        var bar = document.createElement("div");
        bar.className = "tabs";
        bar.setAttribute("role", "tablist");

        panels.forEach(function (panel, i) {
            var btn = document.createElement("button");
            btn.type = "button";
            btn.className = "tab-btn";
            btn.textContent = panel.getAttribute("data-tab-label") || panel.getAttribute("data-tab");
            btn.setAttribute("role", "tab");
            btn.setAttribute("aria-selected", i === 0 ? "true" : "false");
            panel.hidden = i !== 0;

            btn.addEventListener("click", function () {
                panels.forEach(function (p) { p.hidden = true; });
                Array.prototype.forEach.call(bar.children, function (b) {
                    b.setAttribute("aria-selected", "false");
                });
                panel.hidden = false;
                btn.setAttribute("aria-selected", "true");
            });

            bar.appendChild(btn);
        });

        group.insertBefore(bar, group.firstChild);
    });

    /* ---- "on this page" toc, built from the page's own h2 headings ---- */

    var toc = document.getElementById("toc");
    var page = document.querySelector("main.page");

    if (toc && page) {
        var headings = Array.prototype.filter.call(
            page.querySelectorAll("h2"),
            function (h) { return h.offsetParent !== null || !h.closest("[hidden]"); }
        );

        if (headings.length > 1) {
            var nav = document.createElement("nav");
            var used = {};

            headings.forEach(function (h) {
                if (!h.id) {
                    var slug = h.textContent.trim().toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-+|-+$/g, "");
                    if (used[slug]) { slug = slug + "-" + (++used[slug]); } else { used[slug] = 1; }
                    h.id = "toc-" + slug;
                }
                var a = document.createElement("a");
                a.href = "#" + h.id;
                a.textContent = h.textContent;
                a.dataset.target = h.id;
                nav.appendChild(a);
            });

            toc.appendChild(nav);

            var links = Array.prototype.slice.call(nav.querySelectorAll("a"));
            if ("IntersectionObserver" in window) {
                var visible = {};
                var observer = new IntersectionObserver(function (entries) {
                    entries.forEach(function (entry) {
                        visible[entry.target.id] = entry.isIntersecting;
                    });
                    var topMost = headings.find(function (h) { return visible[h.id]; });
                    links.forEach(function (a) {
                        a.classList.toggle("active", !!topMost && a.dataset.target === topMost.id);
                    });
                }, { rootMargin: "-80px 0px -70% 0px" });

                headings.forEach(function (h) { observer.observe(h); });
            }
        } else {
            toc.style.display = "none";
        }
    } else if (toc) {
        toc.style.display = "none";
    }

    /* ---- copy-to-clipboard button on every code block ---- */

    Array.prototype.forEach.call(document.querySelectorAll("pre"), function (pre) {
        var codeEl = pre.querySelector("code") || pre;
        var btn = document.createElement("button");
        btn.type = "button";
        btn.className = "copy-btn";
        btn.textContent = "Copy";
        btn.addEventListener("click", function () {
            var text = codeEl.textContent;
            var done = function () {
                btn.textContent = "Copied";
                btn.classList.add("copied");
                setTimeout(function () {
                    btn.textContent = "Copy";
                    btn.classList.remove("copied");
                }, 1400);
            };
            if (navigator.clipboard && navigator.clipboard.writeText) {
                navigator.clipboard.writeText(text).then(done, done);
            } else {
                done();
            }
        });
        pre.appendChild(btn);
    });
})();
