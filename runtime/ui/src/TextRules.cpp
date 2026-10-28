// XDB text rewrite rules: controller wording -> keyboard/mouse wording, all five game languages (0 EN, 1 DE, 2 FR, 3
// IT, 4 ES). See docs/reference/ui-prompts.md section 4. The strings are the game's raw bytes (Windows-1252); literals
// below are written in UTF-8 and converted by L().
#include "ui/Prompts.h"
#include <algorithm>
#include <cctype>
#include <cstring>

namespace drakan_ui {
namespace {

enum Lang { EN = 0, DE = 1, FR = 2, IT = 3, ES = 4 };

// UTF-8 literal -> Windows-1252 bytes (all accented letters used here are in the Latin-1 range)
std::string L(const char* u8) {
    std::string r;
    for (const unsigned char* p = (const unsigned char*)u8; *p; ++p) {
        if (*p < 0x80)
            r += (char)*p;
        else if ((*p & 0xe0) == 0xc0 && p[1]) {
            r += (char)(((*p & 0x1f) << 6) | (p[1] & 0x3f));
            ++p;
        } else
            r += '?';
    }
    return r;
}

void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty())
        return;
    for (size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size())
        s.replace(p, from.size(), to);
}
bool isWordChar(char c) {
    return std::isalnum((unsigned char)c) != 0;
}
bool endsWith(const std::string& s, const std::string& t) {
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}
bool before(const std::string& s, size_t p, const char* w) {
    size_t n = std::strlen(w);
    return p >= n && s.compare(p - n, n, w) == 0;
}

// ---- how a bound source is called in a language
// --------------------------------------------------------------------------------- name: for a plain key just its name
// ("E", "Strg", "Esc"); with whole = true a complete noun phrase ("Leertaste", "la barra spaziatrice", "il tasto
// sinistro del mouse"; German mouse buttons carry a '%' for the adjective ending: link%s -> "linke" / "linken").
struct Loc {
    std::string name;
    bool whole = false;
};

struct KeyRow {
    const char* cap;
    const char* de;
    const char* fr;
    const char* it;
    const char* es;
}; // a leading '*' marks a whole phrase
const KeyRow kKeys[] = {
    {"SPC", "*Leertaste", "Espace", "*la barra spaziatrice", "*la barra espaciadora"},
    {"CTL", "Strg", "Ctrl", "Ctrl", "Ctrl"},
    {"SHF", "*Umschalttaste", "Maj", "Maiusc", "May\xc3\xbas"},
    {"ALT", "Alt", "Alt", "Alt", "Alt"},
    {"TAB", "Tab", "Tab", "Tab", "Tab"},
    {"ENT", "*Eingabetaste",
     "Entr\xc3\xa9"
     "e",
     "Invio", "Intro"},
    {"ESC", "Esc",
     "\xc3\x89"
     "chap",
     "Esc", "Esc"},
    {"BSP",
     "*R\xc3\xbc"
     "cktaste",
     "Retour arri\xc3\xa8re", "Backspace", "Retroceso"},
    {"CAP", "*Feststelltaste", "Verr. maj", "Bloc Maiusc", "Bloq May\xc3\xbas"},
    {"HOM", "Pos1",
     "D\xc3\xa9"
     "but",
     "Home", "Inicio"},
    {"END", "Ende", "Fin", "Fine", "Fin"},
    {"PGU", "Bild auf",
     "Page pr\xc3\xa9"
     "c.",
     "Pag su", "Re P\xc3\xa1g"},
    {"PGD", "Bild ab", "Page suiv.", "Pag gi\xc3\xb9", "Av P\xc3\xa1g"},
    {"INS", "Einfg", "Ins", "Ins", "Insert"},
    {"DEL", "Entf", "Suppr", "Canc", "Supr"},
};

Loc locOf(const Label& l, int lang) {
    Loc r;
    if (l.kind == Label::Mouse) {
        static const char* de[] = {
            "", "link%s Maustaste", "recht%s Maustaste", "mittler%s Maustaste", "Maustaste 4", "Maustaste 5"};
        static const char* fr[] = {"",
                                   "le bouton gauche de la souris",
                                   "le bouton droit de la souris",
                                   "le bouton central de la souris",
                                   "le bouton 4 de la souris",
                                   "le bouton 5 de la souris"};
        static const char* it[] = {"",
                                   "il tasto sinistro del mouse",
                                   "il tasto destro del mouse",
                                   "il tasto centrale del mouse",
                                   "il tasto 4 del mouse",
                                   "il tasto 5 del mouse"};
        static const char* es[] = {"",
                                   "el bot\xc3\xb3n izquierdo del rat\xc3\xb3n",
                                   "el bot\xc3\xb3n derecho del rat\xc3\xb3n",
                                   "el bot\xc3\xb3n central del rat\xc3\xb3n",
                                   "el bot\xc3\xb3n 4 del rat\xc3\xb3n",
                                   "el bot\xc3\xb3n 5 del rat\xc3\xb3n"};
        int n = std::max(1, std::min(5, l.mouseButton));
        r.whole = true;
        r.name = L(lang == DE ? de[n] : lang == FR ? fr[n] : lang == IT ? it[n] : es[n]);
        return r;
    }
    if (l.kind == Label::Wheel) {
        bool up = l.arrow == 0;
        r.whole = true;
        r.name =
            L(lang == DE   ? (up ? "Mausrad nach oben" : "Mausrad nach unten")
              : lang == FR ? (up ? "la molette vers le haut" : "la molette vers le bas")
              : lang == IT ? (up ? "la rotellina verso l'alto" : "la rotellina verso il basso")
                           : (up ? "la rueda del rat\xc3\xb3n hacia arriba" : "la rueda del rat\xc3\xb3n hacia abajo"));
        return r;
    }
    if (l.kind == Label::ArrowKey) {
        static const char* de[] = {"*Pfeiltaste nach oben", "*Pfeiltaste nach unten", "*Pfeiltaste nach links",
                                   "*Pfeiltaste nach rechts"};
        static const char* fr[] = {"Fl\xc3\xa8"
                                   "che haut",
                                   "Fl\xc3\xa8"
                                   "che bas",
                                   "Fl\xc3\xa8"
                                   "che gauche",
                                   "Fl\xc3\xa8"
                                   "che droite"};
        static const char* it[] = {"Freccia su", "Freccia gi\xc3\xb9", "Freccia sinistra", "Freccia destra"};
        static const char* es[] = {"Flecha arriba", "Flecha abajo", "Flecha izquierda", "Flecha derecha"};
        std::string n = L(lang == DE ? de[l.arrow] : lang == FR ? fr[l.arrow] : lang == IT ? it[l.arrow] : es[l.arrow]);
        if (!n.empty() && n[0] == '*') {
            r.whole = true;
            n.erase(0, 1);
        }
        r.name = n;
        return r;
    }
    for (const KeyRow& k : kKeys) {
        if (l.cap != k.cap)
            continue;
        std::string n = L(lang == DE ? k.de : lang == FR ? k.fr : lang == IT ? k.it : k.es);
        if (!n.empty() && n[0] == '*') {
            r.whole = true;
            n.erase(0, 1);
        }
        r.name = n;
        return r;
    }
    r.name = l.name; // letters, digits, F-keys, punctuation, numpad: language independent
    return r;
}

std::string capFirst(std::string s) {
    if (!s.empty())
        s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}
std::string stripArticle(const std::string& s) { // "la barra spaziatrice" -> "barra spaziatrice"
    for (const char* a : {"la ", "il ", "le ", "el ", "los ", "las "})
        if (s.compare(0, std::strlen(a), a) == 0)
            return s.substr(std::strlen(a));
    return s;
}

// The noun phrase for a token. noun = false: just the name (hint bars: "Esc to exit"); dative: German dative (after
// "der").
std::string phrase(const Loc& x, int lang, bool noun, bool dative, bool plural = false) {
    std::string n = x.name;
    if (lang == DE) {
        size_t p = n.find('%');
        if (p != std::string::npos)
            n.replace(p, 2, dative ? "en" : "e");
        if (x.whole)
            return noun ? n : capFirst(n);
        return noun ? n + "-Taste" : n;
    }
    if (x.whole)
        return noun ? n : capFirst(stripArticle(n));
    if (!noun)
        return n;
    if (lang == FR)
        return "la touche " + n;
    if (lang == IT)
        return "il tasto " + n;
    return plural ? "las teclas " + n : "la tecla " + n;
}

// ---- English
// --------------------------------------------------------------------------------------------------------------------
// "L1 button" -> "E key" (or just the mouse button name), "the L1" -> "the E key"
bool replaceTokenEn(std::string& s, const std::string& token, const Label& l) {
    bool changed = false, mouse = l.kind == Label::Mouse || l.kind == Label::Wheel;
    for (size_t p = 0; (p = s.find(token, p)) != std::string::npos;) {
        size_t e = p + token.size();
        bool wordStart = p == 0 || !isWordChar(s[p - 1]);
        bool wordEnd = e >= s.size() || !isWordChar(s[e]);
        if (!wordStart || !wordEnd) {
            p = e;
            continue;
        }
        std::string rep = l.name;
        size_t consume = 0;
        if (s.compare(e, 7, " button") == 0) {
            consume = 7;
            rep = mouse ? l.name : l.name + " key";
        } else if (before(s, p, "the ") && !mouse)
            rep = l.name + " key";
        s.replace(p, token.size() + consume, rep);
        p += rep.size();
        changed = true;
    }
    return changed;
}

// ---- DE / FR / IT / ES
// ----------------------------------------------------------------------------------------------------------- Replaces
// every whole-word `token` (plus the noun the sentence already has in front of / behind it) by the language's phrase.
// bareOnly: hint bar strings ("START zum Verlassen"): no noun is added.
void replaceTokenLang(std::string& s, const std::string& token, const Label& l, int lang, bool bareOnly) {
    Loc x = locOf(l, lang);
    for (size_t p = 0; (p = s.find(token, p)) != std::string::npos;) {
        size_t e = p + token.size();
        bool wordStart = p == 0 || !isWordChar(s[p - 1]);
        bool wordEnd = e >= s.size() || !isWordChar(s[e]);
        if (!wordStart || !wordEnd) {
            p = e;
            continue;
        }
        size_t b = p; // start of the span that is replaced (includes an existing noun in front)
        std::string rep;
        if (lang == DE) {
            bool dative = before(s, p, "der ") || before(s, p, "dem ");
            if (s.compare(e, 6, "-Taste") == 0) {
                e += 6;
                rep = phrase(x, lang, true, dative);
            } else if (s.compare(e, 7, "- oder ") == 0 || s.compare(e, 6, "- und ") == 0) {
                e += 1;
                rep = phrase(x, lang, true, dative);
            } else if (bareOnly)
                rep = phrase(x, lang, false, dative);
            else
                rep = "die " + phrase(x, lang, true, false); // "Drücken Sie " -> "... die Eingabetaste"
        } else {
            size_t pre = 0;
            if (lang == FR)
                pre = before(s, p, "la touche ") ? 10 : before(s, p, "touche ") ? 7 : 0;
            if (lang == IT)
                pre = before(s, p, "il tasto ") ? 9 : before(s, p, "tasto ") ? 6 : 0;
            bool plural = false;
            if (lang == ES && before(s, p, "los botones ")) {
                pre = 12;
                plural = true;
            }
            if (lang == ES && pre == 0) {
                std::string a = L("el bot\xc3\xb3n "), c = L("bot\xc3\xb3n ");
                pre = before(s, p, a.c_str()) ? a.size() : before(s, p, c.c_str()) ? c.size() : 0;
            }
            b = p - pre;
            bool noun = !bareOnly || pre > 0;
            rep = phrase(x, lang, noun, false, plural);
        }
        s.replace(b, e - b, rep);
        p = b + rep.size();
    }
}

std::string dirKeys(const Bindings& b, const char* prefix) {
    std::string r;
    bool arrows = true;
    for (const char* d : {"UP", "LEFT", "DOWN", "RIGHT"}) {
        Label l = b.primary(std::string(prefix) + d);
        if (!l.bound())
            return {};
        arrows = arrows && l.kind == Label::ArrowKey;
        if (!r.empty())
            r += " ";
        r += l.cap;
    }
    return arrows ? std::string("arrow keys") : r + " keys";
}

// ---- PS2 memory card wording -> PC save wording (all five languages)
// --------------------------------------------------------------- The disc talks about a "Memory Card (PS2)" in "MEMORY
// CARD slot 1" (needs %dKB free, "Do not remove", formatting ...). On the PC the saves are files next to the exe (mc0 /
// mc1 folders), so the strings are replaced by their save-folder equivalents. Placeholders (%d) and the Cross glyph
// escape keep their place; strings the PC build can never show (format prompts) are reworded too, for consistency.
// Report with before / after: build/ui_polish/memcard_text_report.txt.
bool memcardText(unsigned id, int lang, std::string& text) {
    struct M {
        unsigned id;
        const char* t[5];
    };
    static const M rows[] = {
        {6,
         {"Saving. Please wait", "Bitte warten, Spielstand wird gespeichert", "Sauvegarde en cours, veuillez patienter",
          "Salvataggio in corso, attendere", "Guardando, por favor espera"}},
        {67,
         {"Drakan: The Ancients' Gates requires at least %dKB of free disk space for save data in order to progress "
          "beyond the first area of gameplay and to progress to each subsequent area of gameplay.",
          "Drakan II ben\xc3\xb6tigt mindestens %dKB freien Speicherplatz f\xc3\xbcr Spielst\xc3\xa4nde, um \xc3\xbc"
          "ber den 1. und alle folgenden Spiel-Level hinaus voranzuschreiten.",
          "Drakan II n\xc3\xa9"
          "cessite au moins %dKo d'espace libre pour les sauvegardes afin de progresser au-del\xc3\xa0 de la "
          "premi\xc3\xa8re zone de jeu puis jusqu'aux zones de jeu suivantes.",
          "Drakan II necessita di almeno %dKB di spazio libero per i salvataggi affinch\xc3\xa9 il gioco superi la "
          "prima area di gioco e la partita possa continuare.",
          "Drakan II necesita al menos %dKB de espacio libre para partidas guardadas para poder superar la primera "
          "zona de juego y avanzar a las siguientes."}},
        {70,
         {"The save location has enough free space.", "Auf dem Speicherort ist gen\xc3\xbcgend Platz vorhanden.",
          "L'emplacement de sauvegarde dispose de l'espace n\xc3\xa9"
          "cessaire.",
          "La posizione di salvataggio ha lo spazio necessario.",
          "La ubicaci\xc3\xb3n de guardado tiene el espacio necesario."}},
        {71,
         {"No save location is available.", "Es ist kein Speicherort verf\xc3\xbcgbar.",
          "Aucun emplacement de sauvegarde disponible.", "Nessuna posizione di salvataggio disponibile.",
          "No hay ninguna ubicaci\xc3\xb3n de guardado disponible."}},
        {72,
         {"There is not enough free space for save data (%dKB required).",
          "Es ist nicht gen\xc3\xbcgend Speicherplatz f\xc3\xbcr Spielst\xc3\xa4nde vorhanden (%dKB ben\xc3\xb6tigt).",
          "Espace insuffisant pour les sauvegardes (%dKo requis).",
          "Spazio libero insufficiente per i salvataggi (%dKB necessari).",
          "No hay suficiente espacio libre para guardar partidas (%dKB necesarios)."}},
        {74,
         {"Please make sure the save folder is available or press \\033 to proceed anyway.",
          "Bitte stellen Sie sicher, dass der Speicherordner verf\xc3\xbcgbar ist, oder dr\xc3\xbc"
          "cken Sie \\033, um trotzdem fortzufahren.",
          "Veuillez v\xc3\xa9rifier que le dossier de sauvegarde est disponible ou appuyez sur \\033 pour continuer.",
          "Assicurati che la cartella di salvataggio sia disponibile o premi \\033 per continuare comunque.",
          "Por favor, comprueba que la carpeta de guardado est\xc3\xa1 disponible o pulsa \\033 para continuar de "
          "todas formas."}},
        {107,
         {"Loading saved games...", "Gespeicherte Spielst\xc3\xa4nde werden geladen...",
          "Chargement des sauvegardes...", "Caricamento dei salvataggi...", "Cargando partidas guardadas..."}},
        {108,
         {"Save folder not found!", "Speicherordner nicht gefunden!", "Dossier de sauvegarde introuvable !",
          "Cartella di salvataggio non trovata!", "\xc2\xa1No se ha encontrado la carpeta de guardado!"}},
        {109,
         {"In order to save, the game's save folder must be available",
          "Zum Speichern muss der Speicherordner des Spiels verf\xc3\xbcgbar sein",
          "Pour sauvegarder, le dossier de sauvegarde du jeu doit \xc3\xaatre disponible",
          "Per salvare, la cartella di salvataggio del gioco deve essere disponibile",
          "Para guardar, la carpeta de guardado del juego debe estar disponible"}},
        {110,
         {"Save slot %d is not set up. Set it up now?", "Speicherplatz %d ist nicht eingerichtet. Jetzt einrichten?",
          "L'emplacement de sauvegarde %d n'est pas initialis\xc3\xa9. L'initialiser maintenant ?",
          "La posizione di salvataggio %d non \xc3\xa8 inizializzata. Inizializzarla ora?",
          "La ubicaci\xc3\xb3n de guardado %d no est\xc3\xa1 inicializada. \xc2\xbf"
          "Deseas inicializarla ahora?"}},
        {111,
         {"Save slot %d is not set up.", "Speicherplatz %d ist nicht eingerichtet.",
          "L'emplacement de sauvegarde %d n'est pas initialis\xc3\xa9.",
          "La posizione di salvataggio %d non \xc3\xa8 inizializzata.",
          "La ubicaci\xc3\xb3n de guardado %d no est\xc3\xa1 inicializada."}},
        {129,
         {"Reading save data...", "Spielst\xc3\xa4nde werden gelesen ...", "Lecture des sauvegardes...",
          "Lettura dei salvataggi...", "Leyendo partidas guardadas..."}},
        {167,
         {"Some settings were changed.  Save the new settings?",
          "Einige Einstellungen wurden ver\xc3\xa4ndert. Die neuen Einstellungen speichern?",
          "Des r\xc3\xa9glages ont \xc3\xa9t\xc3\xa9 modifi\xc3\xa9s. Sauvegarder les nouveaux r\xc3\xa9glages ?",
          "Alcune impostazioni sono state modificate. Vuoi salvare le nuove impostazioni?",
          "Algunas configuraciones han cambiado. \xc2\xbf"
          "Deseas guardar la nueva configuraci\xc3\xb3n?"}},
        {170,
         {"In order to progress beyond this point the game must be saved.",
          "Es muss gespeichert werden, um von hier aus weiterzuspielen.",
          "La partie doit \xc3\xaatre sauvegard\xc3\xa9"
          "e pour pouvoir continuer.",
          "Per poter proseguire oltre questo punto \xc3\xa8 necessario salvare la partita.",
          "Para seguir m\xc3\xa1s all\xc3\xa1 de este punto tienes que guardar la partida."}},
        {171,
         {"Loading options data. Please wait", "Ladevorgang Optionen-Daten. Bitte warten",
          "Chargement donn\xc3\xa9"
          "es Options. Veuillez patienter",
          "Caricamento dati opzioni. Attendere", "Cargando datos de opciones. Por favor, espera"}},
    };
    for (const M& m : rows) {
        if (m.id != id)
            continue;
        text = L(m.t[lang]);
        return true;
    }
    return false;
}

} // namespace

bool rewriteText(const Bindings& b, const std::string& file, unsigned id, int lang, std::string& text) {
    const std::string orig = text;
    const bool intf = file == "INTERFCE.XDB";
    if (intf && lang >= 0 && lang <= 4)
        memcardText(id, lang, text); // memory card wording -> save folder wording (before the key-name rules below)
    const bool hintBar = intf && id >= 35 && id <= 38; // "\033 Load, START to Exit": bare key names
    // menu prompts: the Cross glyph escape \033 means "confirm" there, but the glyph itself is the gameplay Cross key.
    // Spell out the menu key.
    Label confirm = b.primary("CROSS", true);
    const char kConfirm[] = "\x01";
    bool hasConfirm = false;
    if (intf && (id == 35 || id == 36 || id == 73 || id == 74 || id == 85) && confirm.bound() &&
        text.find("\\033") != std::string::npos) {
        replaceAll(text, "\\033", kConfirm);
        hasConfirm = true;
    }
    if (lang == ES && endsWith(text, L("los botones L1")))
        text.replace(text.size() - 14, 14, "L1 y R1"); // truncated on the disc (INTERFCE 148)

    if (lang == EN) {
        std::string dk = dirKeys(b, "DPAD_");
        if (!dk.empty())
            replaceAll(text, "directional buttons", dk);
        std::string mk = dirKeys(b, "LSTICK_");
        if (!mk.empty())
            replaceAll(text, "left analog stick", mk);
        if (b.sources("RSTICK_UP").empty())
            replaceAll(text, "right stick", "mouse"); // look = mouse
    } else if (lang == ES) {
        replaceAll(text, L("uno de los botones de direcci\xc3\xb3n"), L("una de las teclas de direcci\xc3\xb3n"));
        replaceAll(text, L("los botones de direcci\xc3\xb3n"), L("las teclas de direcci\xc3\xb3n"));
    }
    if (intf) { // options menu: Controller / Show Controller (Above) / Controller Configuration
        struct R {
            unsigned id;
            int lang;
            const char* from;
            const char* to;
        };
        static const R rules[] = {
            {141, EN, "Controller Configuration", "Control Scheme"},
            {130, EN, "Controller", "Controls"},
            {138, EN, "Controller", "Controls"},
            {130, DE, "Controller", "Steuerung"},
            {138, DE, "Controller", "Steuerung"},
            {141, DE, "Controller-Konfiguration", "Steuerungsschema"},
            {130, FR, "Manette", "Commandes"},
            {138, FR, "Montrer manette", "Montrer les commandes"},
            {141, FR, "de la manette", "des commandes"},
            {130, IT, "Controller", "Controlli"},
            {138, IT, "Mostra controller", "Mostra i controlli"},
            {141, IT, "Configurazione controller", "Configurazione dei controlli"},
            {130, ES, "Mando", "Controles"},
            {138, ES, "Mostrar mando", "Mostrar controles"},
            {141, ES, "del mando", "de controles"},
        };
        for (const R& r : rules)
            if (r.id == id && r.lang == lang)
                replaceAll(text, r.from, r.to);
    }
    struct T {
        const char* token;
        const char* action;
    };
    static const T toks[] = {{"L1", "L1"}, {"R1", "R1"},         {"L2", "L2"},
                             {"R2", "R2"}, {"SELECT", "SELECT"}, {"START", "START"}};
    for (const T& t : toks) {
        const bool pageKey = std::strcmp(t.action, "L1") == 0 || std::strcmp(t.action, "R1") == 0;
        Label l = b.primary(t.action, intf && pageKey); // the inventory help names L1 / R1 = the section switch keys of
                                                        // the MENU layer (Page Up / Page Down), not the gameplay ones
        if (!l.bound())
            continue;
        if (lang == EN)
            replaceTokenEn(text, t.token, l);
        else {
            replaceTokenLang(text, t.token, l, lang, hintBar);
            if (lang == FR && std::strcmp(t.token, "SELECT") == 0)
                replaceTokenLang(text, "Select", l, lang, false); // French writes "Select"
        }
    }
    if (lang == ES)
        replaceAll(text, "pulsado la tecla", "pulsada la tecla"); // "Mantén pulsado R2" -> "Mantén pulsada la tecla F"
    if (hasConfirm) {
        if (lang == EN) {
            replaceAll(text, kConfirm, confirm.name);
        } else
            replaceTokenLang(text, kConfirm, confirm, lang, hintBar);
    }
    return text != orig;
}

hle::XdbTextOverride::Rule makeTextRule(const Bindings& b) {
    auto bind = std::make_shared<Bindings>(b);
    return [bind](const hle::ResourceId& id, int lang, std::string& text) {
        return rewriteText(*bind, id.file, id.id, lang, text);
    };
}

} // namespace drakan_ui
