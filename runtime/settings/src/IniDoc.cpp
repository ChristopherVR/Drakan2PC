#include "settings/IniDoc.h"

#include <algorithm>
#include <cctype>

namespace dsettings {

std::string IniDoc::lower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string IniDoc::trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

void IniDoc::parseLine(Line& l, const std::string& cur) const {
    l.section = cur;
    const std::string& t = l.text;
    size_t i = 0;
    while (i < t.size() && (t[i] == ' ' || t[i] == '\t'))
        ++i;
    if (i >= t.size()) {
        l.kind = Line::Blank;
        return;
    }
    if (t[i] == ';' || t[i] == '#') {
        l.kind = Line::Comment;
        return;
    }
    if (t[i] == '[') {
        size_t r = t.find(']', i);
        l.kind = Line::Section;
        l.section = lower(trim(t.substr(i + 1, r == std::string::npos ? std::string::npos : r - i - 1)));
        return;
    }
    size_t eq = t.find('=', i);
    if (eq == std::string::npos) {
        l.kind = Line::Other;
        return;
    }
    l.kind = Line::Pair;
    l.indent = t.substr(0, i);
    size_t ke = eq;
    while (ke > i && (t[ke - 1] == ' ' || t[ke - 1] == '\t'))
        --ke;
    l.key = t.substr(i, ke - i);
    l.preEq = t.substr(ke, eq - ke);
    size_t vs = eq + 1;
    while (vs < t.size() && (t[vs] == ' ' || t[vs] == '\t'))
        ++vs;
    l.postEq = t.substr(eq + 1, vs - eq - 1);
    // trailing comment: comment char preceded by whitespace
    size_t cpos = std::string::npos;
    for (size_t k = vs; k < t.size(); ++k)
        if (commentChars_.find(t[k]) != std::string::npos && k > vs && (t[k - 1] == ' ' || t[k - 1] == '\t')) {
            cpos = k;
            break;
        }
    std::string rest = t.substr(vs, cpos == std::string::npos ? std::string::npos : cpos - vs);
    size_t ve = rest.size();
    while (ve > 0 && (rest[ve - 1] == ' ' || rest[ve - 1] == '\t'))
        --ve;
    l.value = rest.substr(0, ve);
    if (cpos != std::string::npos) {
        l.gap = rest.substr(ve);
        l.comment = t.substr(cpos);
    } else {
        l.gap.clear();
        l.comment.clear();
        // whitespace after the value without comment: kept implicitly by text (not rewritten unless dirty)
    }
}

std::string IniDoc::render(const Line& l) const {
    if (!l.dirty || l.kind != Line::Pair)
        return l.text;
    std::string s = l.indent + l.key + l.preEq + "=" + l.postEq + l.value;
    if (!l.comment.empty())
        s += (l.gap.empty() ? std::string(" ") : l.gap) + l.comment;
    return s;
}

IniDoc IniDoc::parse(const std::string& textIn, const std::string& commentChars) {
    IniDoc d(commentChars);
    std::string text = textIn;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        d.bom_ = true;
        text.erase(0, 3);
    }
    size_t crlf = 0, lf = 0;
    for (size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\n') {
            if (i > 0 && text[i - 1] == '\r')
                ++crlf;
            else
                ++lf;
        }
    d.eol_ = (crlf == 0 && lf == 0) ? "\r\n" : (crlf >= lf ? "\r\n" : "\n");
    d.trailingEol_ = text.empty() || text.back() == '\n';
    std::string cur;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        std::string ln = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (!ln.empty() && ln.back() == '\r')
            ln.pop_back();
        Line l;
        l.text = ln;
        d.parseLine(l, cur);
        if (l.kind == Line::Section)
            cur = l.section;
        d.lines_.push_back(std::move(l));
        if (e == std::string::npos) {
            d.trailingEol_ = false;
            break;
        }
        pos = e + 1;
    }
    return d;
}

std::string IniDoc::serialize() const {
    std::string out;
    if (bom_)
        out = "\xEF\xBB\xBF";
    bool first = true;
    for (const Line& l : lines_) {
        if (l.deleted)
            continue;
        if (!first)
            out += eol_;
        first = false;
        out += render(l);
    }
    if (!first && trailingEol_)
        out += eol_;
    return out;
}

bool IniDoc::empty() const {
    for (const auto& l : lines_)
        if (!l.deleted)
            return false;
    return true;
}

int IniDoc::findLast(const std::string& section, const std::string& key) const {
    const std::string s = lower(section), k = lower(key);
    for (int i = static_cast<int>(lines_.size()) - 1; i >= 0; --i) {
        const Line& l = lines_[i];
        if (!l.deleted && l.kind == Line::Pair && l.section == s && lower(l.key) == k)
            return i;
    }
    return -1;
}
bool IniDoc::has(const std::string& s, const std::string& k) const {
    return findLast(s, k) >= 0;
}
std::string IniDoc::get(const std::string& s, const std::string& k, const std::string& def) const {
    int i = findLast(s, k);
    return i < 0 ? def : lines_[i].value;
}

int IniDoc::sectionHeader(const std::string& section) const {
    const std::string s = lower(section);
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
        if (!lines_[i].deleted && lines_[i].kind == Line::Section && lines_[i].section == s)
            return i;
    return -1;
}
bool IniDoc::hasSection(const std::string& section) const {
    return sectionHeader(section) >= 0;
}

int IniDoc::insertionPoint(const std::string& section) const {
    const std::string s = lower(section);
    int hdr = sectionHeader(s);
    if (hdr < 0)
        return -1;
    // The body runs until the next section header; insert after the last Pair/Other/Comment line that is directly
    // attached to the section content. Trailing blank lines (and comments belonging to the next section are not
    // detectable) stay after.
    int end = static_cast<int>(lines_.size());
    for (int i = hdr + 1; i < static_cast<int>(lines_.size()); ++i)
        if (!lines_[i].deleted && lines_[i].kind == Line::Section) {
            end = i;
            break;
        }
    int last = hdr;
    for (int i = hdr + 1; i < end; ++i)
        if (!lines_[i].deleted && lines_[i].kind == Line::Pair)
            last = i;
    return last + 1;
}

void IniDoc::set(const std::string& section, const std::string& key, const std::string& value,
                 const std::string& newComment) {
    int i = findLast(section, key);
    if (i >= 0) {
        if (lines_[i].value == value)
            return;
        setLineValue(i, value);
        return;
    }
    // new key (the comment goes on its own line above it)
    const int at = appendEntry(section, key, value);
    if (!newComment.empty()) {
        Line c;
        c.text = "; " + newComment;
        c.kind = Line::Comment;
        c.section = lower(section);
        lines_.insert(lines_.begin() + at, c);
    }
}

void IniDoc::setLineValue(int line, const std::string& value) {
    Line& l = lines_[line];
    if (l.kind != Line::Pair)
        return;
    l.value = value;
    l.dirty = true;
    if (l.comment.empty())
        l.gap.clear();
    l.text = render(l);
}

void IniDoc::deleteLine(int line) {
    lines_[line].deleted = true;
}

void IniDoc::remove(const std::string& section, const std::string& key) {
    const std::string s = lower(section), k = lower(key);
    for (auto& l : lines_)
        if (!l.deleted && l.kind == Line::Pair && l.section == s && lower(l.key) == k)
            l.deleted = true;
}

int IniDoc::appendEntry(const std::string& section, const std::string& key, const std::string& value,
                        const std::string& trailingComment) {
    int at = insertionPoint(section);
    if (at < 0) {
        // new section at end of file, separated by a blank line
        int lastLive = -1;
        for (int i = static_cast<int>(lines_.size()) - 1; i >= 0; --i)
            if (!lines_[i].deleted) {
                lastLive = i;
                break;
            }
        if (lastLive >= 0 && lines_[lastLive].kind != Line::Blank) {
            Line b;
            b.kind = Line::Blank;
            lines_.push_back(b);
        }
        Line h;
        h.text = "[" + section + "]";
        h.kind = Line::Section;
        h.section = lower(section);
        lines_.push_back(h);
        at = static_cast<int>(lines_.size());
        if (!trailingEol_)
            trailingEol_ = true;
    }
    Line n;
    n.kind = Line::Pair;
    n.section = lower(section);
    n.key = key;
    n.preEq = " ";
    n.postEq = " ";
    n.value = value;
    if (!trailingComment.empty()) {
        n.gap = "  ";
        n.comment = "; " + trailingComment;
    }
    n.dirty = true;
    n.text = render(n);
    lines_.insert(lines_.begin() + at, std::move(n));
    return at;
}

std::vector<IniDoc::KV> IniDoc::entries(const std::string& section) const {
    std::vector<KV> out;
    const std::string s = lower(section);
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const Line& l = lines_[i];
        if (!l.deleted && l.kind == Line::Pair && l.section == s)
            out.push_back({i, l.key, l.value, l.comment});
    }
    return out;
}

void IniDoc::replaceEntries(const std::string& section, const std::vector<std::pair<std::string, std::string>>& kv) {
    const std::string s = lower(section);
    for (auto& l : lines_)
        if (!l.deleted && l.kind == Line::Pair && l.section == s)
            l.deleted = true;
    if (kv.empty() && !hasSection(section))
        return;
    for (auto& p : kv)
        appendEntry(section, p.first, p.second);
}

} // namespace dsettings
