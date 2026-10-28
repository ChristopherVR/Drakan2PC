// IniDoc: a comment/format preserving ini document (std only, platform free).
//
// Parses the same dialect as drakan_config.cpp (drakan.ini) and input/Config.cpp (drakan_input.ini):
//   [section] / key = value / full line comments starting with ';' or '#' / trailing comments "value ; text".
// Every line keeps its original text; only lines that are changed through set()/setLineValue()/remove are rewritten,
// so comments, blank lines, key spelling, spacing, unknown keys/sections and the EOL style (CRLF or LF) of the file
// survive a save.
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace dsettings {

class IniDoc {
public:
    // commentChars: characters that start a trailing comment when preceded by whitespace
    //   drakan.ini : ";"   (drakan_config.cpp only recognises " ;")
    //   input ini  : ";#"  (Config.cpp)
    explicit IniDoc(std::string commentChars = ";") : commentChars_(std::move(commentChars)) {}

    static IniDoc parse(const std::string& text, const std::string& commentChars = ";");
    std::string serialize() const;

    // ---- key access (section/key compared case-insensitively; last assignment wins like the readers do) ----
    bool has(const std::string& section, const std::string& key) const;
    std::string get(const std::string& section, const std::string& key, const std::string& def = "") const;
    // Replaces the value in place (keeping indentation, spacing and trailing comment) or appends "key = value" at the
    // end of the section (creating the section at the end of the file when it does not exist). `newComment` is written
    // above a new key.
    void set(const std::string& section, const std::string& key, const std::string& value,
             const std::string& newComment = "");
    void remove(const std::string& section, const std::string& key); // removes all assignments of the key

    // ---- line level access (used for [bindings], where a key may repeat) ----
    struct KV {
        int line;
        std::string key, value, comment;
    };
    std::vector<KV> entries(const std::string& section) const; // live (not deleted) key/value lines of a section
    bool hasSection(const std::string& section) const;
    void setLineValue(int line, const std::string& value); // keeps key/spacing/comment
    void deleteLine(int line);
    // Inserts "key = value" after the last non-blank line of the section body (before trailing blank lines); creates
    // the section if needed. Returns the new line index.
    int appendEntry(const std::string& section, const std::string& key, const std::string& value,
                    const std::string& trailingComment = "");
    // Replaces a whole section body (used when the section does not exist yet or a layer was reset): deletes the old
    // key lines (comment lines are kept) and appends the new entries.
    void replaceEntries(const std::string& section, const std::vector<std::pair<std::string, std::string>>& kv);

    bool empty() const;
    const std::string& eol() const { return eol_; }

private:
    struct Line {
        std::string text; // raw text without EOL, authoritative unless `parsed` pieces are stale (dirty)
        enum Kind { Blank, Comment, Section, Pair, Other } kind = Blank;
        std::string section; // lower-case section the line belongs to
        // Pair pieces
        std::string indent, key, preEq, postEq, value, gap, comment;
        bool deleted = false;
        bool dirty = false; // rebuild text from pieces
    };
    std::vector<Line> lines_;
    std::string eol_ = "\r\n";
    bool trailingEol_ = true;
    bool bom_ = false;
    std::string commentChars_;

    static std::string lower(std::string s);
    static std::string trim(const std::string& s);
    void parseLine(Line& l, const std::string& currentSection) const;
    std::string render(const Line& l) const;
    int findLast(const std::string& section, const std::string& key) const;
    int sectionHeader(const std::string& section) const;  // index of the header line or -1
    int insertionPoint(const std::string& section) const; // line index at which a new key is inserted
};

} // namespace dsettings
