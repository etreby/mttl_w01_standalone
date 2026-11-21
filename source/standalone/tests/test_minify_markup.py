"""Unit tests for the device dashboard CSS/HTML minifier."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from minify_markup import minify_css, minify_html, usage  # noqa: E402


class CssTests(unittest.TestCase):
    def test_whitespace_and_final_semicolon(self):
        self.assertEqual(minify_css(' a { color : red ; margin : 0 0 18px ; }\n/* note */ b{x:1}'),
                         'a{color:red;margin:0 0 18px}b{x:1}')

    def test_hex_colours_only_in_declarations(self):
        self.assertEqual(minify_css('#aabbcc{color:#AABBCC;background:#0000000d;border-color:#11223344;fill:#abcdef}'),
                         '#aabbcc{color:#ABC;background:#0000000d;border-color:#1234;fill:#abcdef}')

    def test_values_keep_significant_spaces(self):
        css = 'header{padding:18px max(20px,calc((100% - 1152px)/2))}'
        self.assertEqual(minify_css(css), css)

    def test_selector_combinators_and_media(self):
        self.assertEqual(minify_css('@media (max-width: 600px) { #a > p , .b  .c { x:1 } }'),
                         '@media(max-width:600px){#a>p,.b .c{x:1}}')

    def test_dead_selectors_are_removed(self):
        html = '<div id="log" class="panel row"><p>x</p></div>'
        used = usage(html, "el.className='switch is-on';el.id='protect-'+k;document.createElement('b')")
        css = ('.panel{a:1}.gone{b:2}.row,.missing{c:3}#log{d:4}#nope{e:5}progress{f:6}b{g:7}'
               '.switch.is-on{h:8}.x:has(.gone){i:9}#protect-power{j:10}nav button[aria-selected=true]{k:11}'
               '@media(max-width:1px){.gone{l:12}}:root{m:13}*{n:14}')
        self.assertEqual(minify_css(css, used),
                         '.panel{a:1}.row{c:3}#log{d:4}b{g:7}.switch.is-on{h:8}#protect-power{j:10}:root{m:13}*{n:14}')

    def test_no_usage_keeps_everything(self):
        self.assertEqual(minify_css('.gone{b:2}'), '.gone{b:2}')


class HtmlTests(unittest.TestCase):
    def test_plain_values_lose_quotes(self):
        self.assertEqual(minify_html('<input id="a" type="checkbox" maxlength="64" checked>'),
                         '<input id=a type=checkbox maxlength=64 checked>')

    def test_values_that_need_quotes_keep_them(self):
        html = ('<meta content="width=device-width,initial-scale=1"><p aria-label="Event log" data-x="" '
                'title="a\'b" href="/">')
        self.assertEqual(minify_html(html), html)

    def test_script_and_style_are_untouched(self):
        html = '<style>a[x="1"]{}</style><b class="c"></b><script>x(\'<b id="d">\')</script>'
        self.assertEqual(minify_html(html),
                         '<style>a[x="1"]{}</style><b class=c></b><script>x(\'<b id="d">\')</script>')


if __name__ == '__main__':
    unittest.main(verbosity=2)
