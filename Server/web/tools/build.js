#!/usr/bin/env node
"use strict";
// ============================================================
//  Сборка веб-клиента MeetUp: web/  ->  web/dist/
//
//  Зачем. Раньше страница тащила в браузер @babel/standalone (3,1 МБ) и
//  компилировала JSX заново у КАЖДОГО посетителя, плюс dev-сборки React
//  (1,2 МБ). На телефоне по мобильному интернету это была самая заметная
//  часть загрузки — больше, чем всё остальное вместе взятое. Здесь та же
//  самая компиляция делается один раз, при сборке образа.
//
//  Что делает:
//    1. копирует web/ в web/dist/, пропуская babel.js, dev-React и tools/;
//    2. вынимает из каждой страницы блок <script type="text/babel">,
//       компилирует его тем же самым assets/babel.js (под node он UMD и
//       отдаёт себя возвратом require) и кладёт в assets/pages/<стр>.js;
//    3. переписывает страницу: babel.js убран, React — production-сборки,
//       блок заменён на <script src="assets/pages/...">;
//    4. проставляет ?v=<хэш содержимого> КАЖДОЙ ссылке на assets/ —
//       раньше версию поднимали руками, и забытая правка молча уезжала в
//       чужой кэш на сутки (Cache-Control: public, max-age=86400).
//
//  Зависимостей нет: node и уже лежащий в репозитории babel.js. npm install
//  не нужен — и не должен быть нужен: сборка идёт внутри docker build у
//  того, кто поднимает свой сервер, а из инструментов у него только docker.
//
//  Запуск:  node tools/build.js  [--watch]
// ============================================================

const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const ROOT = path.resolve(__dirname, "..");
const OUT = path.join(ROOT, "dist");

// Babel берём ровно тот, что лежит рядом: одна и та же версия компилирует
// код и в браузере (при локальной разработке по исходникам), и здесь.
const Babel = require(path.join(ROOT, "assets", "babel.js"));

// В браузер не едут. React в исходниках остаётся dev-сборкой: при локальной
// разработке она ловит ошибки разметки, ради которых её и держат.
const SKIP_DIRS = new Set(["dist", "tools", ".git"]);
const SKIP_FILES = new Set([
    "assets/babel.js",
    "assets/react.js",
    "assets/react-dom.js",
    "README.md",
]);

// Чем заменяем dev-React на странице.
const PROD_SWAP = [
    ["assets/react-dom.js", "assets/react-dom.production.min.js"],
    ["assets/react.js", "assets/react.production.min.js"],
];

const sha8 = (buf) => crypto.createHash("sha256").update(buf).digest("hex").slice(0, 8);
const kb = (n) => (n / 1024).toFixed(0) + " KB";

// walk по исходникам — с пропусками; walkAll по dist — без них.
function walk(dir, base, skip) {
    const out = [];
    for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const rel = base ? base + "/" + entry.name : entry.name;
        if (entry.isDirectory()) {
            if (skip && SKIP_DIRS.has(entry.name)) continue;
            out.push.apply(out, walk(path.join(dir, entry.name), rel, skip));
        } else if (!(skip && SKIP_FILES.has(rel))) {
            out.push(rel);
        }
    }
    return out;
}

// Каталог dist НЕ сносим целиком. Репозиторий вполне может лежать в
// синхронизируемой папке (OneDrive и подобные держат открытый хэндл), и
// rmSync на корне падает с EPERM — проверено на живой машине. Поэтому
// пишем поверх, а лишнее вычищаем пофайлово, с повторами: отдельный файл
// освобождается, даже когда каталог занят.
function pruneStale(expected) {
    if (!fs.existsSync(OUT)) return;
    for (const rel of walk(OUT, "", false)) {
        if (expected.has(rel)) continue;
        try {
            fs.rmSync(path.join(OUT, rel), { force: true, maxRetries: 5, retryDelay: 80 });
        } catch (e) {
            console.warn("  ! не удалось убрать устаревший dist/" + rel + ": " + e.code);
        }
    }
}

// Вынуть единственный блок <script type="text/babel"> со страницы.
// Литерала "</script>" внутри JSX быть не может (он оборвал бы тег ещё в
// браузере), но проверяем: молча собрать половину страницы — худший исход.
function extractBabel(html, page) {
    const open = /<script\s+type="text\/babel"\s*>/i.exec(html);
    if (!open) return null;
    const from = open.index + open[0].length;
    const to = html.indexOf("</script>", from);
    if (to < 0) throw new Error(page + ": блок text/babel не закрыт");
    if (/<script\s+type="text\/babel"/i.test(html.slice(to)))
        throw new Error(page + ": блоков text/babel больше одного — сборщик ждёт один");
    return { code: html.slice(from, to), start: open.index, end: to + "</script>".length };
}

// Только preset "react". Рантайм-Babel добавлял ещё "env" (то есть спуск до
// ES5) — здесь это чистые потери: клиент и так требует WebCodecs и
// AudioWorklet, а их нет ни в одном браузере без ES2020.
function compileJsx(code, page) {
    return Babel.transform(code, {
        presets: ["react"],
        filename: page,
        compact: false,
        comments: true,
    }).code;
}

function build() {
    const t0 = Date.now();
    fs.mkdirSync(OUT, { recursive: true });

    const files = walk(ROOT, "", true);
    const pages = [];
    const written = new Set();
    let jsxBytes = 0;

    // ---- 1. копируем всё, кроме страниц (их соберём следом) ----
    for (const rel of files) {
        const dst = path.join(OUT, rel);
        fs.mkdirSync(path.dirname(dst), { recursive: true });
        if (rel.endsWith(".html")) { pages.push(rel); continue; }
        fs.copyFileSync(path.join(ROOT, rel), dst);
        written.add(rel);
    }

    // ---- 2. страницы: компилируем JSX, правим ссылки ----
    for (const rel of pages) {
        let html = fs.readFileSync(path.join(ROOT, rel), "utf8");
        const block = extractBabel(html, rel);

        if (block) {
            const name = path.basename(rel, ".html");
            const outRel = "assets/pages/" + name + ".js";
            fs.mkdirSync(path.join(OUT, "assets", "pages"), { recursive: true });
            fs.writeFileSync(path.join(OUT, outRel),
                "// Собрано из " + rel + " сборщиком tools/build.js.\n"
                + "// Править исходную страницу, а не этот файл.\n"
                + compileJsx(block.code, rel));
            written.add(outRel);
            jsxBytes += Buffer.byteLength(block.code);

            html = html.slice(0, block.start)
                 + '<script src="' + outRel + '"></script>'
                 + html.slice(block.end);
            html = html.replace(/[ \t]*<script src="assets\/babel\.js"><\/script>\r?\n?/g, "");
        }

        for (const pair of PROD_SWAP) html = html.split(pair[0]).join(pair[1]);

        // ?v= по содержимому: забыть поднять версию больше нельзя.
        html = html.replace(/(src|href)="(assets\/[^"?#]+)(\?[^"]*)?"/g, function (m, attr, file) {
            const p = path.join(OUT, file);
            if (!fs.existsSync(p)) {
                console.warn("  ! " + rel + ": ссылка на несуществующий " + file);
                return m;
            }
            return attr + '="' + file + "?v=" + sha8(fs.readFileSync(p)) + '"';
        });

        fs.writeFileSync(path.join(OUT, rel), html);
        written.add(rel);
    }

    // ---- 3. убираем то, что осталось от прошлых сборок ----
    pruneStale(written);

    // ---- 4. отчёт ----
    const size = (root, rel) => {
        const p = path.join(root, rel);
        return fs.existsSync(p) ? fs.statSync(p).size : 0;
    };
    let total = 0;
    for (const rel of walk(OUT, "", false)) total += size(OUT, rel);
    const dropped = size(ROOT, "assets/babel.js") + size(ROOT, "assets/react.js")
                  + size(ROOT, "assets/react-dom.js");
    const added = size(OUT, "assets/react.production.min.js")
                + size(OUT, "assets/react-dom.production.min.js");

    console.log("web/dist собран за " + (Date.now() - t0) + " ms: "
        + pages.length + " pages, " + kb(total) + " total.");
    console.log("  browser saves " + kb(dropped) + " (babel + dev React), gets back "
        + kb(added) + " production React; " + kb(jsxBytes)
        + " of JSX no longer compiled on the device.");
}

// Рекурсивный fs.watch есть на Windows и macOS всегда, а на Linux — только с
// node 19.1. Внутри docker слежение и не нужно (сборка там разовая), поэтому
// не падаем, а честно говорим, что режима не будет.
function watch() {
    let timer = null;
    if (process.platform === "linux") {
        const v = process.versions.node.split(".").map(Number);
        if (v[0] < 19 || (v[0] === 19 && v[1] < 1)) {
            console.error("--watch недоступен: рекурсивное слежение требует node 19.1+"
                + " (у вас " + process.versions.node + ") — пересобирайте вручную.");
            return;
        }
    }
    fs.watch(ROOT, { recursive: true }, function (_ev, file) {
        if (!file) return;
        const rel = String(file).replace(/\\/g, "/");
        if (rel.indexOf("dist/") === 0 || rel.indexOf("tools/") === 0) return;
        clearTimeout(timer);
        timer = setTimeout(function () {
            try { build(); } catch (e) { console.error("build failed: " + e.message); }
        }, 120);
    });
    console.log("watching web/ — edit sources, dist rebuilds itself. Ctrl+C to stop.");
}

try {
    build();
    if (process.argv.indexOf("--watch") >= 0) watch();
} catch (e) {
    console.error("build failed: " + e.message);
    process.exit(1);
}
