#!/usr/bin/env python3
"""Convert DokuWiki documents to MediaWiki markup (GitHub wiki / Gollum).

The ``*.dokuwiki`` files under ``docs/`` are the single source of truth; the
GitHub wiki pages are generated from them (see
``.github/workflows/publish-wiki.yml``). The committed ``*.mediawiki`` are
therefore build artifacts and are git-ignored.

Usage:
    # write <name>.mediawiki next to each source
    python3 docs/dokuwiki2mediawiki.py docs/*.dokuwiki

    # write generated pages into a separate directory
    python3 docs/dokuwiki2mediawiki.py --out-dir _wiki docs/*.dokuwiki

    # explicit single-file form
    python3 docs/dokuwiki2mediawiki.py SRC.dokuwiki DST.mediawiki

Only the Python standard library is used (no third-party deps), so it runs
as-is on a stock GitHub Actions ``ubuntu-latest`` runner.
"""
import argparse
import os
import re
import sys

SMILEYS = [':!:', ':-D', ':-)', ';-)', ':?:', ':-(', '8-)']


def convert_inline(text):
    # 1. stash links so inline markup never touches URLs / targets
    links = []

    def stash_link(m):
        inner = m.group(1)
        if '|' in inner:
            target, label = inner.split('|', 1)
            label = label.strip()
        else:
            target, label = inner, None
        target = target.strip()
        if re.match(r'^(https?|ftp)://', target) or target.startswith('mailto:'):
            out = '[%s %s]' % (target, label) if label else target
        else:
            target = re.sub(r'\.dokuwiki$', '', target)  # point at wiki page
            out = '[[%s|%s]]' % (target, label) if label else '[[%s]]' % target
        links.append(out)
        return '\x00L%d\x00' % (len(links) - 1)

    text = re.sub(r'\[\[(.+?)\]\]', stash_link, text)

    # 2. drop decorative DokuWiki smileys
    for s in SMILEYS:
        text = text.replace(s + ' ', '').replace(' ' + s, '').replace(s, '')

    # 3. inline emphasis (order matters: monospace before bold/italic)
    text = re.sub(r"''(.+?)''", lambda m: '<code>%s</code>' % m.group(1), text)
    text = re.sub(r'\*\*(.+?)\*\*', lambda m: "'''%s'''" % m.group(1), text)
    text = re.sub(r'(?<!:)//(.+?)//', lambda m: "''%s''" % m.group(1), text)

    # 4. restore links
    text = re.sub(r'\x00L(\d+)\x00', lambda m: links[int(m.group(1))], text)
    return text.rstrip()


def parse_row(line):
    line = line.strip()
    seps = [i for i, ch in enumerate(line) if ch in '^|']
    cells = []
    for k in range(len(seps) - 1):
        cells.append((line[seps[k]], line[seps[k] + 1:seps[k + 1]].strip()))
    return cells


def flush_table(buf, out):
    if not buf:
        return
    out.append('{| class="wikitable"')
    for raw in buf:
        cells = parse_row(raw)
        out.append('|-')
        if cells and all(sep == '^' for sep, _ in cells):
            out.append('! ' + ' !! '.join(convert_inline(c) for _, c in cells))
        else:
            out.append('| ' + ' || '.join(convert_inline(c) for _, c in cells))
    out.append('|}')
    buf.clear()


def flush_quote(buf, out):
    if not buf:
        return
    out.append('<blockquote>')
    pending = []

    def emit_para():
        if pending:
            out.append(convert_inline(' '.join(pending)))
            pending.clear()

    for q in buf:
        if q.strip() == '':
            emit_para()
            out.append('')
        else:
            pending.append(q.strip())
    emit_para()
    out.append('</blockquote>')
    buf.clear()


def convert_line(line):
    m = re.match(r'^(={2,6})\s*(.*?)\s*\1\s*$', line)
    if m:
        level = 7 - len(m.group(1))            # dokuwiki 6 ==> mw 1
        mark = '=' * level
        return '%s %s %s' % (mark, convert_inline(m.group(2)), mark)
    if re.match(r'^\s*-{4,}\s*$', line) or re.match(r'^\s*-\s+-{3,}\s*$', line):
        return '----'
    m = re.match(r'^(\s*)([*-])\s+(.*)$', line)
    if m:
        level = max(1, len(m.group(1)) // 2)
        mark = ('*' if m.group(2) == '*' else '#') * level
        return '%s %s' % (mark, convert_inline(m.group(3)))
    return convert_inline(line.lstrip())


def convert(text):
    out, table_buf, quote_buf = [], [], []
    in_code, closer = False, None
    for raw in text.split('\n'):
        if in_code:
            if re.match(r'^</code>\s*$', raw):
                out.append(closer)
                in_code = False
            else:
                out.append(raw)
            continue
        cm = re.match(r'^<code(?:\s+(\S+))?>\s*$', raw)
        if cm:
            flush_table(table_buf, out)
            flush_quote(quote_buf, out)
            if cm.group(1):
                out.append('<syntaxhighlight lang="%s">' % cm.group(1))
                closer = '</syntaxhighlight>'
            else:
                out.append('<pre>')
                closer = '</pre>'
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
                and out and re.match(r'^[*#]+ ', out[-1])):
            out[-1] = out[-1] + ' ' + convert_inline(raw.strip())
            continue
        out.append(convert_line(raw))
    flush_table(table_buf, out)
    flush_quote(quote_buf, out)
    result = '\n'.join(out)
    result = re.sub(r'\n{3,}', '\n\n', result)
    return result.rstrip() + '\n'


def convert_file(src, dst):
    with open(src, encoding='utf-8') as f:
        data = f.read()
    with open(dst, 'w', encoding='utf-8') as f:
        f.write(convert(data))
    print('wrote', dst)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('inputs', nargs='+', help='one or more *.dokuwiki files')
    p.add_argument('--out-dir', help='write generated pages into this directory')
    args = p.parse_args(argv)

    # explicit two-arg form: SRC.dokuwiki DST.mediawiki
    if (args.out_dir is None and len(args.inputs) == 2
            and args.inputs[1].endswith('.mediawiki')):
        convert_file(args.inputs[0], args.inputs[1])
        return 0

    if args.out_dir:
        os.makedirs(args.out_dir, exist_ok=True)
    for src in args.inputs:
        base = os.path.basename(src)
        base = re.sub(r'\.dokuwiki$', '', base) + '.mediawiki'
        dst = os.path.join(args.out_dir, base) if args.out_dir \
            else re.sub(r'\.dokuwiki$', '.mediawiki', src)
        convert_file(src, dst)
    return 0


if __name__ == '__main__':
    sys.exit(main())
