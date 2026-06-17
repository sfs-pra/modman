#!/usr/bin/env python3
"""Convert DokuWiki documents to GitHub-Flavored Markdown (GitHub wiki).

GitHub wikis render Markdown natively and cleanly; the MediaWiki renderer
mangles inline ``<code>`` (it HTML-armors ``-``/``{``/``}`` into numeric
entities that GitHub does not decode). Markdown backticks have no such issue,
so the wiki pages are generated as ``.md``.

The ``*.dokuwiki`` files are the single source of truth; generated ``.md``
pages are build artifacts (see the publish-wiki workflow).

Usage:
    python3 docs/dokuwiki2markdown.py --out-dir _wiki docs/*.dokuwiki

Standard library only (runs as-is on a stock GitHub Actions ubuntu runner).
"""
import argparse
import os
import re
import sys
from collections.abc import Sequence
from typing import cast

SMILEYS = [':!:', ':-D', ':-)', ';-)', ':?:', ':-(', '8-)']
HEADING_RE = re.compile(r'^(#{1,6})\s+(.+?)\s*$')


def convert_inline(text: str) -> str:
    links: list[str] = []

    def stash_link(m: re.Match[str]) -> str:
        inner = m.group(1)
        if '|' in inner:
            target, label = inner.split('|', 1)
            label = label.strip()
        else:
            target, label = inner, None
        target = target.strip()
        if re.match(r'^(https?|ftp)://', target) or target.startswith('mailto:'):
            out = '[%s](%s)' % (label, target) if label else '<%s>' % target
        else:
            target = re.sub(r'\.dokuwiki$', '', target)
            out = '[%s](%s)' % (label or target, target)
        links.append(out)
        return '\x00L%d\x00' % (len(links) - 1)

    text = re.sub(r'\[\[(.+?)\]\]', stash_link, text)

    for s in SMILEYS:
        text = text.replace(s + ' ', '').replace(' ' + s, '').replace(s, '')

    text = text.replace('<del>', '~~').replace('</del>', '~~')
    text = re.sub(r'<code>(.+?)</code>', lambda m: '`%s`' % m.group(1), text)
    text = re.sub(r'<([^<>]+)>', lambda m: '`<%s>`' % m.group(1), text)
    text = re.sub(r"''(.+?)''", lambda m: '`%s`' % m.group(1), text)
    text = re.sub(r'(?<!:)//(.+?)//', lambda m: '*%s*' % m.group(1), text)

    text = re.sub(r'\x00L(\d+)\x00', lambda m: links[int(m.group(1))], text)
    return text.rstrip()


def parse_row(line: str) -> list[str]:
    line = line.strip()
    seps = [i for i, ch in enumerate(line) if ch in '^|']
    cells: list[str] = []
    for k in range(len(seps) - 1):
        cells.append(line[seps[k] + 1:seps[k + 1]].strip())
    return cells


def flush_table(buf: list[str], out: list[str]) -> None:
    if not buf:
        return
    header = parse_row(buf[0])
    out.append('| ' + ' | '.join(convert_inline(c) for c in header) + ' |')
    out.append('| ' + ' | '.join('---' for _ in header) + ' |')
    for raw in buf[1:]:
        cells = parse_row(raw)
        out.append('| ' + ' | '.join(convert_inline(c) for c in cells) + ' |')
    buf.clear()


def flush_quote(buf: list[str], out: list[str]) -> None:
    if not buf:
        return
    for q in buf:
        out.append('> ' + convert_inline(q) if q.strip() else '>')
    buf.clear()


def heading_label(text: str) -> str:
    text = re.sub(r'\[([^\]]+)\]\([^)]+\)', r'\1', text)
    text = re.sub(r'[`*_~]', '', text)
    return text.strip()


def heading_anchor(text: str) -> str:
    label = heading_label(text).lower()
    label = re.sub(r'[^\w\s-]', '', label, flags=re.UNICODE)
    label = re.sub(r'[\s-]+', '-', label).strip('-')
    return label


def add_table_of_contents(lines: list[str]) -> list[str]:
    headings: list[tuple[int, str, str]] = []
    seen: dict[str, int] = {}
    first_heading_index: int | None = None

    for i, line in enumerate(lines):
        m = HEADING_RE.match(line)
        if not m:
            continue
        if first_heading_index is None:
            first_heading_index = i
            continue
        level = len(m.group(1))
        label = heading_label(m.group(2))
        anchor = heading_anchor(m.group(2))
        count = seen.get(anchor, 0)
        seen[anchor] = count + 1
        if count:
            anchor = '%s-%d' % (anchor, count)
        headings.append((level, label, anchor))

    if first_heading_index is None or not headings:
        return lines

    title = 'Содержание' if re.search(r'[А-Яа-яЁё]', lines[first_heading_index]) else 'Contents'
    toc = ['## %s' % title, '']
    baseline_levels = [level for level, _, _ in headings if level > 1]
    min_level = min(baseline_levels or [level for level, _, _ in headings])
    for level, label, anchor in headings:
        indent = '  ' * max(0, level - min_level)
        toc.append('%s- [%s](#%s)' % (indent, label, anchor))
    toc.append('')
    return lines[:first_heading_index + 1] + [''] + toc + lines[first_heading_index + 1:]


def convert_line(line: str) -> str:
    m = re.match(r'^(={2,6})\s*(.*?)\s*\1\s*$', line)
    if m:
        return '%s %s' % ('#' * (7 - len(m.group(1))), convert_inline(m.group(2)))
    if re.match(r'^\s*-{4,}\s*$', line) or re.match(r'^\s*-\s+-{3,}\s*$', line):
        return '---'
    m = re.match(r'^(\s*)([*-])\s+(.*)$', line)
    if m:
        level = max(1, len(m.group(1)) // 2)
        indent = '  ' * (level - 1)
        bullet = '-' if m.group(2) == '*' else '1.'
        return '%s%s %s' % (indent, bullet, convert_inline(m.group(3)))
    return convert_inline(line.lstrip())


def convert(text: str) -> str:
    out: list[str] = []
    table_buf: list[str] = []
    quote_buf: list[str] = []
    in_code = False
    for raw in text.split('\n'):
        if in_code:
            if re.match(r'^</code>\s*$', raw):
                out.append('```')
                in_code = False
            else:
                out.append(raw)
            continue
        cm = re.match(r'^<code(?:\s+(\S+))?>\s*$', raw)
        if cm:
            flush_table(table_buf, out)
            flush_quote(quote_buf, out)
            out.append('```' + (cm.group(1) or 'text'))
            in_code = True
            continue
        if raw.strip() and re.match(r'^\s*[\^|]', raw):
            flush_quote(quote_buf, out)
            table_buf.append(raw)
            continue
        flush_table(table_buf, out)
        qm = re.match(r'^>\s?(.*)$', raw)
        if qm:
            quote_buf.append(qm.group(1))
            continue
        flush_quote(quote_buf, out)
        if (raw[:1] in (' ', '\t') and raw.strip()
                and not re.match(r'^(\s*)([*-])\s+', raw)
                and not re.match(r'^\s*(={2,6})', raw)
                and not re.match(r'^\s*-{4,}\s*$', raw)
                and not re.match(r'^\s*-\s+-{3,}\s*$', raw)
                and out and re.match(r'^(\s*)([*-]|\d+\.)\s', out[-1])):
            out[-1] = out[-1] + ' ' + convert_inline(raw.strip())
            continue
        out.append(convert_line(raw))
    flush_table(table_buf, out)
    flush_quote(quote_buf, out)
    out = add_table_of_contents(out)
    result = '\n'.join(out)
    result = re.sub(r'\n{3,}', '\n\n', result)
    return result.rstrip() + '\n'


def convert_file(src: str, dst: str) -> None:
    with open(src, encoding='utf-8') as f:
        data = f.read()
    with open(dst, 'w', encoding='utf-8') as f:
        _ = f.write(convert(data))
    print('wrote', dst)


def main(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    _ = p.add_argument('inputs', nargs='+', help='one or more *.dokuwiki files')
    _ = p.add_argument('--out-dir', required=True, help='directory for generated .md pages')
    args = p.parse_args(argv)
    out_dir = cast(str, args.out_dir)
    inputs = cast(list[str], args.inputs)
    os.makedirs(out_dir, exist_ok=True)
    for src in inputs:
        base = re.sub(r'\.dokuwiki$', '', os.path.basename(src)) + '.md'
        convert_file(src, os.path.join(out_dir, base))
    return 0


if __name__ == '__main__':
    sys.exit(main())
