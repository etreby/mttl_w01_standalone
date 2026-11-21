"""Build-time CSS/HTML minification for the device dashboard.

Only transformations that cannot change rendering or behaviour are applied:
whitespace and final-semicolon removal, hex colour shortening inside
declarations, removal of selectors that reference a class, ID or element
type that appears nowhere in the shipped page or scripts, and dropping
quotes around attribute values that need none. Element IDs, classes and
attribute values are never renamed.
"""
import re

_HEX = re.compile(r'#([0-9a-fA-F])\1([0-9a-fA-F])\2([0-9a-fA-F])\3(?:([0-9a-fA-F])\4)?(?![0-9a-fA-F])')


def _blocks(css):
    """Split CSS into (prelude, body) pairs at the top level."""
    out, i = [], 0
    while i < len(css):
        start = css.index('{', i)
        depth, j = 1, start + 1
        while depth:
            depth += {'{': 1, '}': -1}.get(css[j], 0)
            j += 1
        out.append((css[i:start].strip(), css[start + 1:j - 1]))
        i = j
        while i < len(css) and css[i].isspace():
            i += 1
    return out


def _alive(selector, used):
    tokens = re.findall(r'([.#]?)(-?[A-Za-z_][\w-]*)', re.sub(r'\[[^\]]*\]|::?[\w-]+(?:\([^)]*\))?', ' ', selector))
    for kind, name in tokens:
        if kind == '.' and name not in used['classes']:
            return False
        if kind == '#' and name not in used['ids']:
            return False
        if kind == '' and name not in used['tags']:
            return False
    return True


def _declarations(body):
    body = re.sub(r'\s*([:;,])\s*', r'\1', body.strip()).rstrip(';')
    return _HEX.sub(lambda m: '#' + ''.join(g for g in m.groups() if g), body)


def minify_css(css, used=None):
    css = re.sub(r'/\*.*?\*/', '', css, flags=re.S).strip()
    out = []
    for prelude, body in _blocks(css):
        prelude = re.sub(r'\s+', ' ', prelude)
        if prelude.startswith('@'):
            inner = minify_css(body, used)
            if inner:
                out.append(re.sub(r'\s*([(:,)])\s*', r'\1', prelude) + '{' + inner + '}')
            continue
        selectors = [s.strip() for s in prelude.split(',')]
        if used is not None:
            selectors = [s for s in selectors if _alive(s, used)]
        if selectors:
            selectors = [re.sub(r'\s*([>+~])\s*', r'\1', s) for s in selectors]
            out.append(','.join(selectors) + '{' + _declarations(body) + '}')
    return ''.join(out)


def usage(html, scripts):
    """Collect the classes, IDs and element types the page can ever contain.

    Scripts may only name classes and IDs through string literals; any word in
    any string literal (or a literal prefix ending in '-') counts as used.
    """
    literals = re.findall(r"""'((?:[^'\\\n]|\\.)*)'|"((?:[^"\\\n]|\\.)*)"|`((?:[^`\\]|\\.)*)`""", scripts)
    words = {w for group in literals for text in group for w in re.findall(r'[\w-]+', text)}
    attrs = re.findall(r'''\b(?:class|id)=(?:"([^"]*)"|([^\s>]+))''', html)
    names = words | {w for pair in attrs for value in pair for w in value.split()}
    prefixes = tuple(w for w in words if w.endswith('-'))
    return {
        'classes': _Names(names, prefixes),
        'ids': _Names(names, prefixes),
        'tags': set(re.findall(r'<([a-z][a-z0-9]*)', html)) |
                set(re.findall(r'''createElement\(\s*['"]([a-z][a-z0-9]*)['"]''', scripts)) |
                {w for w in words if re.fullmatch(r'[a-z][a-z0-9]*', w)} |
                {'html', 'body'},
    }


class _Names(set):
    def __init__(self, names, prefixes):
        super().__init__(names)
        self.prefixes = prefixes

    def __contains__(self, name):
        return set.__contains__(self, name) or name.startswith(self.prefixes)


_UNQUOTED = re.compile(r'''\s([a-zA-Z-]+)="([^\s"'=<>`]+)"''')


def minify_html(html):
    """Drop quotes around attribute values made of plain characters only."""
    def strip(tag):
        return _UNQUOTED.sub(lambda m: ' %s=%s' % (m[1], m[2]) if not m[2].endswith('/') else m[0], tag[0])
    parts = re.split(r'(<(script|style)\b.*?</\2>)', html, flags=re.S)
    # re.split yields text, block, tag-name triples; never rewrite inside blocks.
    return ''.join(re.sub(r'<[a-zA-Z][^<>]*>', strip, part) if i % 3 == 0 else (part if i % 3 == 1 else '')
                   for i, part in enumerate(parts))
