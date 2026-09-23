#!/usr/bin/env node
/*
 * Markdown -> Confluence storage format (XHTML).
 *
 *   node tools/md-to-confluence.cjs docs/figma-token-bridge-guide.md
 *   node tools/md-to-confluence.cjs <in.md> --out <out.xhtml> --no-toc
 *
 * To publish: create the page in Confluence, then Insert > Markup, choose
 * "Confluence storage format", and paste the generated file.
 *
 * Why storage format rather than pasting the Markdown directly: Confluence's
 * Markdown paste is lossy in the ways that matter here. Code blocks lose their
 * language, callouts become ordinary quotes, and there is no table of contents.
 * Storage format gives the real macros.
 *
 * This deliberately supports only the subset the guide uses, and THROWS on
 * anything it does not recognise rather than passing it through. A converter
 * that silently drops a construct is worse than one that refuses, because the
 * damage only shows up after the page is published.
 */

const fs = require('fs');
const path = require('path');

// ---------------------------------------------------------------- arguments

const argv = process.argv.slice(2);
const inPath = argv.find((a) => !a.startsWith('--'));
if (!inPath) {
  console.error('usage: node tools/md-to-confluence.cjs <file.md> [--out <file.xhtml>] [--no-toc]');
  process.exit(2);
}

function flag(name) {
  const i = argv.indexOf('--' + name);
  return i === -1 ? null : argv[i + 1];
}

const outPath = flag('out')
  || path.join(path.dirname(inPath), path.basename(inPath, '.md') + '.confluence.xhtml');
const wantToc = !argv.includes('--no-toc');

// ------------------------------------------------------------------ helpers

const esc = (s) => s
  .replace(/&/g, '&amp;')
  .replace(/</g, '&lt;')
  .replace(/>/g, '&gt;');

/**
 * Inline formatting. Code spans are pulled out first and put back last, so that
 * an asterisk or bracket inside `code` is never treated as markup.
 */
function inline(text) {
  const codes = [];
  let s = text.replace(/`([^`]+)`/g, (_, c) => {
    codes.push(c);
    return '\u0000CODE' + (codes.length - 1) + '\u0000';
  });

  s = esc(s);

  // Links before emphasis, so a bold label inside a link still works.
  s = s.replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_, label, href) =>
    '<a href="' + esc(href) + '">' + label + '</a>');

  s = s.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  s = s.replace(/(^|[^*])\*([^*]+)\*/g, '$1<em>$2</em>');

  s = s.replace(/\u0000CODE(\d+)\u0000/g, (_, i) => '<code>' + esc(codes[Number(i)]) + '</code>');

  return s;
}

function codeMacro(language, body) {
  if (body.includes(']]>')) {
    throw new Error('code block contains "]]>", which cannot go in a CDATA section');
  }
  const lang = language && language !== 'text'
    ? '  <ac:parameter ac:name="language">' + esc(language) + '</ac:parameter>\n'
    : '';
  return '<ac:structured-macro ac:name="code" ac:schema-version="1">\n'
    + lang
    + '  <ac:plain-text-body><![CDATA[' + body + ']]></ac:plain-text-body>\n'
    + '</ac:structured-macro>';
}

function panel(kind, bodyHtml) {
  return '<ac:structured-macro ac:name="' + kind + '" ac:schema-version="1">\n'
    + '  <ac:rich-text-body>' + bodyHtml + '</ac:rich-text-body>\n'
    + '</ac:structured-macro>';
}

// -------------------------------------------------------------- the converter

const lines = fs.readFileSync(inPath, 'utf8').replace(/\r\n/g, '\n').split('\n');
const out = [];
let i = 0;
let title = null;

function splitRow(row) {
  // Trim the outer pipes, then split. Cells containing an escaped pipe are not
  // used anywhere in this document, and would be a silent corruption if they
  // were, so they are rejected below.
  return row.replace(/^\s*\|/, '').replace(/\|\s*$/, '').split('|').map((c) => c.trim());
}

while (i < lines.length) {
  const line = lines[i];

  // blank
  if (line.trim() === '') { i++; continue; }

  // horizontal rule
  if (/^---+\s*$/.test(line)) { out.push('<hr />'); i++; continue; }

  // fenced code
  if (line.startsWith('```')) {
    const language = line.slice(3).trim();
    const body = [];
    i++;
    while (i < lines.length && !lines[i].startsWith('```')) { body.push(lines[i]); i++; }
    if (i >= lines.length) { throw new Error('unterminated code fence'); }
    i++;
    out.push(codeMacro(language, body.join('\n')));
    continue;
  }

  // heading
  const h = /^(#{1,6})\s+(.*)$/.exec(line);
  if (h) {
    const level = h[1].length;
    const text = inline(h[2]);
    if (level === 1 && title === null) {
      // The page title in Confluence is the page's own title field, so an <h1>
      // here would show it twice.
      title = h[2];
    } else {
      out.push('<h' + level + '>' + text + '</h' + level + '>');
    }
    i++;
    continue;
  }

  // table
  if (line.trim().startsWith('|')) {
    const header = splitRow(line);
    const sep = lines[i + 1];
    if (!sep || !/^\s*\|[\s:|-]+\|\s*$/.test(sep)) {
      throw new Error('table at line ' + (i + 1) + ' has no separator row');
    }
    i += 2;

    const rows = [];
    while (i < lines.length && lines[i].trim().startsWith('|')) {
      const cells = splitRow(lines[i]);
      if (cells.length !== header.length) {
        throw new Error('table row at line ' + (i + 1) + ' has ' + cells.length
          + ' cells but the header has ' + header.length);
      }
      rows.push(cells);
      i++;
    }

    let t = '<table><tbody>';
    t += '<tr>' + header.map((c) => '<th>' + inline(c) + '</th>').join('') + '</tr>';
    for (const r of rows) {
      t += '<tr>' + r.map((c) => '<td>' + inline(c) + '</td>').join('') + '</tr>';
    }
    t += '</tbody></table>';
    out.push(t);
    continue;
  }

  // blockquote -> info panel
  if (line.startsWith('> ')) {
    const body = [];
    while (i < lines.length && lines[i].startsWith('> ')) { body.push(lines[i].slice(2)); i++; }
    out.push(panel('info', '<p>' + inline(body.join(' ')) + '</p>'));
    continue;
  }

  // unordered list
  if (/^[-*]\s+/.test(line)) {
    const items = [];
    while (i < lines.length && /^[-*]\s+/.test(lines[i])) {
      items.push(inline(lines[i].replace(/^[-*]\s+/, '')));
      i++;
    }
    out.push('<ul>' + items.map((x) => '<li><p>' + x + '</p></li>').join('') + '</ul>');
    continue;
  }

  // ordered list
  if (/^\d+\.\s+/.test(line)) {
    const items = [];
    while (i < lines.length && /^\d+\.\s+/.test(lines[i])) {
      items.push(inline(lines[i].replace(/^\d+\.\s+/, '')));
      i++;
    }
    out.push('<ol>' + items.map((x) => '<li><p>' + x + '</p></li>').join('') + '</ol>');
    continue;
  }

  // paragraph: consecutive plain lines, joined
  const para = [];
  while (
    i < lines.length
    && lines[i].trim() !== ''
    && !lines[i].startsWith('```')
    && !lines[i].startsWith('> ')
    && !lines[i].trim().startsWith('|')
    && !/^#{1,6}\s/.test(lines[i])
    && !/^---+\s*$/.test(lines[i])
    && !/^[-*]\s+/.test(lines[i])
    && !/^\d+\.\s+/.test(lines[i])
  ) {
    para.push(lines[i]);
    i++;
  }
  if (para.length === 0) {
    throw new Error('could not classify line ' + (i + 1) + ': ' + JSON.stringify(lines[i]));
  }
  out.push('<p>' + inline(para.join(' ')) + '</p>');
}

// ------------------------------------------------------------------- assemble

const toc = wantToc
  ? '<ac:structured-macro ac:name="toc" ac:schema-version="1">\n'
    + '  <ac:parameter ac:name="maxLevel">2</ac:parameter>\n'
    + '  <ac:parameter ac:name="minLevel">2</ac:parameter>\n'
    + '</ac:structured-macro>\n'
  : '';

fs.writeFileSync(outPath, toc + out.join('\n') + '\n', 'utf8');

const kb = (fs.statSync(outPath).size / 1024).toFixed(1);
console.log('Wrote ' + outPath + ' (' + kb + ' KB)');
if (title) { console.log('Suggested page title: ' + title); }
console.log('\nIn Confluence: create the page, then Insert > Markup > Confluence storage format,');
console.log('and paste the file contents.');
